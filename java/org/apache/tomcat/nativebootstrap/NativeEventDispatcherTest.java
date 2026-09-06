package org.apache.tomcat.nativebootstrap;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/** Standalone regression test for native-event to Executor ownership hand-off. */
public final class NativeEventDispatcherTest {

	private static final ExecutorService EXECUTOR = Executors.newSingleThreadExecutor(r -> {
		Thread thread = new Thread(r, "native-event-test-executor");
		thread.setDaemon(true);
		return thread;
	});
	private static final AtomicInteger processed = new AtomicInteger();
	private static final CountDownLatch completion = new CountDownLatch(1);
	private static final NativeEventDispatcher DISPATCHER = new NativeEventDispatcher(EXECUTOR, (handle, events) -> {
		processed.incrementAndGet();
		completion.countDown();
	});

	private NativeEventDispatcherTest() {
	}

	public static void bootstrap(String[] args) {
	}

	public static void shutdown() {
		DISPATCHER.stop();
		EXECUTOR.shutdownNow();
	}

	public static void dispatchNativeEvent(long connectionHandle, int events) throws InterruptedException {
		DISPATCHER.dispatch(connectionHandle, events);
		if (!completion.await(5, TimeUnit.SECONDS)) {
			throw new IllegalStateException("Executor did not process the native event");
		}
	}

	public static int getProcessed() {
		return processed.get();
	}
}
