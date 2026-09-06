package org.apache.tomcat.util.net;

/**
 * Intermediate NativeTomcat copy of the Tomcat 11.0.25 SocketProcessorBase
 * execution surface. The lock ownership is intentionally preserved.
 */
public abstract class SocketProcessorBase<S> implements Runnable {
    protected SocketWrapperBase<S> socketWrapper;
    protected SocketEvent event;

    protected SocketProcessorBase(SocketWrapperBase<S> socketWrapper, SocketEvent event) {
        this.socketWrapper = socketWrapper;
        this.event = event;
    }

    public void reset(SocketWrapperBase<S> socketWrapper, SocketEvent event) {
        this.socketWrapper = socketWrapper;
        this.event = event;
    }

    @Override
    public final void run() {
        SocketWrapperBase<S> wrapper = socketWrapper;
        if (wrapper == null || wrapper.isClosed()) {
            return;
        }
        wrapper.getLock().lock();
        try {
            if (!wrapper.isClosed()) {
                doRun();
            }
        } finally {
            wrapper.getLock().unlock();
        }
    }

    protected abstract void doRun();
}
