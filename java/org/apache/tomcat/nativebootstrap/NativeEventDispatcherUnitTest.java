package org.apache.tomcat.nativebootstrap;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/** Regression tests for per-connection serialization, coalescing and shutdown draining. */
public final class NativeEventDispatcherUnitTest {

	private NativeEventDispatcherUnitTest() {
	}

	public static void main(String[] args) throws Exception {
		ExecutorService executor = Executors.newFixedThreadPool(4);
		AtomicInteger active = new AtomicInteger();
		AtomicInteger maxActive = new AtomicInteger();
		AtomicInteger calls = new AtomicInteger();
		CountDownLatch completed = new CountDownLatch(1);

		NativeEventDispatcher dispatcher = new NativeEventDispatcher(executor, (handle, events) -> {
			int current = active.incrementAndGet();
			maxActive.accumulateAndGet(current, Math::max);
			calls.incrementAndGet();
			try {
				Thread.sleep(5);
			} catch (InterruptedException e) {
				Thread.currentThread().interrupt();
			}
			active.decrementAndGet();
			completed.countDown();
		});

		for (int i = 0; i < 100; i++) {
			dispatcher.dispatch(7, 1 << (i % 4));
		}

		if (!completed.await(5, TimeUnit.SECONDS)) {
			throw new AssertionError("dispatcher did not execute an event");
		}
		executor.shutdown();
		if (!executor.awaitTermination(5, TimeUnit.SECONDS)) {
			throw new AssertionError("executor did not terminate");
		}
		if (maxActive.get() != 1) {
			throw new AssertionError("connection events executed concurrently: maxActive=" + maxActive.get());
		}
		if (calls.get() >= 100) {
			throw new AssertionError("events were not coalesced: calls=" + calls.get());
		}

		ExecutorService drainExecutor = Executors.newSingleThreadExecutor();
		CountDownLatch started = new CountDownLatch(1);
		CountDownLatch release = new CountDownLatch(1);
		AtomicBoolean stopReturned = new AtomicBoolean();
		NativeEventDispatcher drainDispatcher = new NativeEventDispatcher(drainExecutor, (handle, events) -> {
			started.countDown();
			try {
				release.await(5, TimeUnit.SECONDS);
			} catch (InterruptedException e) {
				Thread.currentThread().interrupt();
			}
		});
		drainDispatcher.dispatch(9, 1);
		if (!started.await(5, TimeUnit.SECONDS)) {
			throw new AssertionError("drain test did not start");
		}

		Thread stopper = new Thread(() -> {
			drainDispatcher.stop();
			stopReturned.set(true);
		}, "native-event-dispatcher-stop-test");
		stopper.start();
		Thread.sleep(50);
		if (stopReturned.get()) {
			throw new AssertionError("dispatcher stop returned before active task drained");
		}
		release.countDown();
		stopper.join(5000);
		if (!stopReturned.get()) {
			throw new AssertionError("dispatcher stop did not return after task drain");
		}
		drainExecutor.shutdown();
		if (!drainExecutor.awaitTermination(5, TimeUnit.SECONDS)) {
			throw new AssertionError("drain executor did not terminate");
		}

		System.out.println("NativeEventDispatcherUnitTest: PASS calls=" + calls.get());
	}
}
