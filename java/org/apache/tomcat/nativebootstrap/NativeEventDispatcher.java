package org.apache.tomcat.nativebootstrap;

import java.util.Objects;
import java.util.concurrent.Executor;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Transfers native readiness notifications to the Tomcat-owned Executor.
 *
 * <p>The native event loop never executes Tomcat processing directly. Events for a connection are coalesced and one
 * runnable at a time is submitted to the Executor, matching Tomcat's SocketProcessor ownership rule that processing
 * for a single socket must not run concurrently.</p>
 */
public final class NativeEventDispatcher {

    @FunctionalInterface
    public interface EventProcessor {
        void process(long connectionHandle, int events);
    }

    private static final class PendingEvent {
        private final AtomicInteger events = new AtomicInteger();
        private final AtomicBoolean scheduled = new AtomicBoolean();
    }

    private final Executor executor;
    private final EventProcessor processor;
    private final ConcurrentMap<Long, PendingEvent> pending = new ConcurrentHashMap<>();
    private volatile boolean accepting = true;

    public NativeEventDispatcher(Executor executor, EventProcessor processor) {
        this.executor = Objects.requireNonNull(executor);
        this.processor = Objects.requireNonNull(processor);
    }

    /**
     * Queue a native event. The caller is the native event-loop thread; it does not run the processor.
     */
    public void dispatch(long connectionHandle, int events) {
        if (!accepting || events == 0) {
            return;
        }

        PendingEvent state = pending.computeIfAbsent(connectionHandle, ignored -> new PendingEvent());
        state.events.getAndAccumulate(events, (current, added) -> current | added);
        schedule(connectionHandle, state);
    }

    /**
     * Stop accepting new events and discard queued state after already submitted tasks drain.
     */
    public void stop() {
        accepting = false;
        pending.clear();
    }

    private void schedule(long connectionHandle, PendingEvent state) {
        if (!state.scheduled.compareAndSet(false, true)) {
            return;
        }

        try {
            executor.execute(() -> run(connectionHandle, state));
        } catch (RuntimeException e) {
            state.scheduled.set(false);
            pending.remove(connectionHandle, state);
            throw e;
        }
    }

    private void run(long connectionHandle, PendingEvent state) {
        try {
            for (;;) {
                int events = state.events.getAndSet(0);
                if (events == 0) {
                    return;
                }
                processor.process(connectionHandle, events);
            }
        } finally {
            state.scheduled.set(false);
            if (state.events.get() != 0 && accepting) {
                schedule(connectionHandle, state);
            } else {
                pending.remove(connectionHandle, state);
            }
        }
    }
}
