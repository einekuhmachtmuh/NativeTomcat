package org.apache.tomcat.nativebootstrap;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.apache.tomcat.util.net.AbstractEndpoint;
import org.apache.tomcat.util.net.NativeSocketProcessor;
import org.apache.tomcat.util.net.NativeSocketWrapper;
import org.apache.tomcat.util.net.SocketEvent;

/** Build/runtime proof for the first SocketWrapperBase/SocketProcessorBase surface. */
public final class SocketWrapperSurfaceTest {
    private SocketWrapperSurfaceTest() { }
    public static void main(String[] args) throws Exception {
        ExecutorService executor = Executors.newFixedThreadPool(4);
        AbstractEndpoint<Long, Object> endpoint = new AbstractEndpoint<>(executor) { };
        NativeSocketWrapper wrapper = new NativeSocketWrapper(42L, endpoint);
        AtomicInteger active = new AtomicInteger();
        AtomicInteger maxActive = new AtomicInteger();
        AtomicReference<Throwable> failure = new AtomicReference<>();

        Runnable action = () -> {
            int activeNow = active.incrementAndGet();
            maxActive.accumulateAndGet(activeNow, Math::max);
            try { Thread.sleep(2); } catch (InterruptedException e) { Thread.currentThread().interrupt(); failure.compareAndSet(null, e); }
            finally { active.decrementAndGet(); }
        };

        for (int i = 0; i < 32; i++) {
            executor.execute(new NativeSocketProcessor(wrapper, SocketEvent.OPEN_READ, action));
        }
        executor.shutdown();
        if (!executor.awaitTermination(10, TimeUnit.SECONDS)) throw new AssertionError("processor executor did not terminate");
        if (failure.get() != null) throw new AssertionError(failure.get());
        if (maxActive.get() != 1) throw new AssertionError("SocketProcessorBase lock did not serialize connection processing: " + maxActive.get());
        if (wrapper.isClosed()) throw new AssertionError("wrapper unexpectedly closed");
        wrapper.registerReadInterest();
        wrapper.registerWriteInterest();
        if (!wrapper.hasReadInterest() || !wrapper.hasWriteInterest()) throw new AssertionError("interest registration surface failed");
        wrapper.close();
        if (!wrapper.isClosed()) throw new AssertionError("close state failed");
        System.out.println("SocketWrapperSurfaceTest: PASS maxActive=" + maxActive.get());
    }
}
