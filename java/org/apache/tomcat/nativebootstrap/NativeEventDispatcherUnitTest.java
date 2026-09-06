package org.apache.tomcat.nativebootstrap;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/** Regression test for per-connection serialization and event coalescing. */
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
		System.out.println("NativeEventDispatcherUnitTest: PASS calls=" + calls.get());
	}
}
