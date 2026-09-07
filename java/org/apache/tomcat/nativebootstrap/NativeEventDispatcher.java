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
    private final Object lifecycleMonitor = new Object();
    private int activeTasks;
    private boolean accepting = true;

    public NativeEventDispatcher(Executor executor, EventProcessor processor) {
        this.executor = Objects.requireNonNull(executor);
        this.processor = Objects.requireNonNull(processor);
    }

    /**
     * Queue a native event. The caller is the native event-loop thread; it does not run the processor.
     */
    public void dispatch(long connectionHandle, int events) {
        PendingEvent state;
        boolean submit;

        synchronized (lifecycleMonitor) {
            if (!accepting || events == 0) {
                return;
            }

            state = pending.computeIfAbsent(connectionHandle, ignored -> new PendingEvent());
            state.events.getAndAccumulate(events, (current, added) -> current | added);
            submit = state.scheduled.compareAndSet(false, true);
            if (submit) {
                activeTasks++;
            }
        }

        if (submit) {
            try {
                executor.execute(() -> run(connectionHandle, state));
            } catch (RuntimeException e) {
                synchronized (lifecycleMonitor) {
                    state.scheduled.set(false);
                    pending.remove(connectionHandle, state);
                    activeTasks--;
                    lifecycleMonitor.notifyAll();
                }
                throw e;
            }
        }
    }

    /**
     * Stop accepting new events, discard queued state and wait for submitted processors to finish.
     *
     * <p>The wait is required because native connection objects remain runtime-owned while Tomcat worker tasks may
     * still be consuming them. Runtime teardown must not free those objects before the Java-side processing has
     * drained.</p>
     */
    public void stop() {
        synchronized (lifecycleMonitor) {
            accepting = false;
            pending.clear();
            while (activeTasks != 0) {
                try {
                    lifecycleMonitor.wait();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("interrupted while draining native event tasks", e);
                }
            }
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
            synchronized (lifecycleMonitor) {
                state.scheduled.set(false);
                if (state.events.get() != 0 && accepting) {
                    state.scheduled.set(true);
                    return;
                }
                pending.remove(connectionHandle, state);
                activeTasks--;
                lifecycleMonitor.notifyAll();
            }
        }
    }
}
