package org.apache.tomcat.util.net;

import java.io.IOException;
import java.util.concurrent.TimeUnit;

import org.apache.juli.logging.Log;
import org.apache.juli.logging.LogFactory;
import org.apache.tomcat.util.collections.SynchronizedStack;

/**
 * Tomcat-side endpoint boundary for connections owned by the NativeTomcat
 * native event loop.
 *
 * <p>The native runtime owns the listener, accepted file descriptors and
 * readiness/rearm operations. This endpoint owns only the Tomcat-side
 * SocketWrapper registry and the hand-off into Tomcat's normal
 * {@link #processSocket(SocketWrapperBase, SocketEvent, boolean)} dispatch
 * path.</p>
 *
 * <p>This is deliberately not an implementation of Tomcat's Java acceptor or
 * selector model. Native accept and readiness integration remain separate
 * gates.</p>
 */
public final class NativeEndpoint extends AbstractEndpoint<Long, Long> {

    private static final Log log = LogFactory.getLog(NativeEndpoint.class);

    /**
     * Create a native endpoint. The native runtime remains the accept owner.
     */
    public NativeEndpoint() {
        // Keep AbstractEndpoint's lifecycle contract satisfied for code paths
        // that expect an Acceptor object to exist. This acceptor is never
        // started; native code owns accept.
        acceptor = new Acceptor<>(this);
    }

    /**
     * Register a native connection with the Tomcat-side connection registry.
     *
     * @param nativeHandle Stable native connection handle
     * @return The existing or newly created wrapper
     */
    public NativeSocketWrapper registerNativeConnection(long nativeHandle) {
        if (nativeHandle == 0) {
            throw new IllegalArgumentException("native connection handle must be non-zero");
        }

        SocketWrapperBase<Long> existing = connections.get(nativeHandle);
        if (existing != null) {
            return (NativeSocketWrapper) existing;
        }

        NativeSocketWrapper created = new NativeSocketWrapper(nativeHandle, this);
        SocketWrapperBase<Long> raced = connections.putIfAbsent(nativeHandle, created);
        if (raced == null) {
            return created;
        }
        created.close();
        return (NativeSocketWrapper) raced;
    }

    /**
     * Deliver a native readiness notification through Tomcat's normal
     * SocketProcessor dispatch boundary.
     *
     * @param nativeHandle Stable native connection handle
     * @param events Native runtime event mask
     * @return {@code true} if processing was submitted successfully
     */
    public boolean processNativeEvent(long nativeHandle, int events) {
        NativeSocketWrapper wrapper = registerNativeConnection(nativeHandle);
        SocketEvent event = toSocketEvent(events);
        if (event == null) {
            return false;
        }
        return processSocket(wrapper, event, true);
    }

    private SocketEvent toSocketEvent(int events) {
        // NativeTomcat's current event mask uses bit 0 for readable and bit 1
        // for writable. Peer-read-closed and error are terminal conditions and
        // are deliberately mapped to Tomcat ERROR until native close semantics
        // are integrated in the next lifecycle gate.
        if ((events & 0x4) != 0 || (events & 0x8) != 0) {
            return SocketEvent.ERROR;
        }
        if ((events & 0x1) != 0) {
            return SocketEvent.OPEN_READ;
        }
        if ((events & 0x2) != 0) {
            return SocketEvent.OPEN_WRITE;
        }
        return null;
    }

    @Override
    protected SocketProcessorBase<Long> createSocketProcessor(SocketWrapperBase<Long> socketWrapper, SocketEvent event) {
        return new SocketProcessor(socketWrapper, event);
    }

    @Override
    public void bind() {
        // The native runtime owns the listening socket. NativeEndpoint is only
        // the Tomcat-side endpoint contract at this stage.
    }

    @Override
    public void startInternal() {
        if (!running) {
            running = true;
            paused = false;
            if (processorCache == null && getSocketProperties().getProcessorCache() != 0) {
                processorCache = new SynchronizedStack<>(SynchronizedStack.DEFAULT_SIZE,
                        getSocketProperties().getProcessorCache());
            }
            if (getExecutor() == null) {
                createExecutor();
            }
            initializeConnectionLatch();
        }
    }

    @Override
    public void stopInternal() {
        if (running) {
            running = false;
            paused = true;
            if (acceptor != null) {
                acceptor.stopMillis(0);
            }
            shutdownExecutor();
            if (processorCache != null) {
                processorCache.clear();
                processorCache = null;
            }
        }
    }

    @Override
    protected Log getLog() {
        return log;
    }

    @Override
    protected void doCloseServerSocket() throws IOException {
        // No Java server socket exists. Native runtime owns this resource.
    }

    @Override
    protected Long serverSocketAccept() {
        throw new UnsupportedOperationException("native runtime owns accept");
    }

    @Override
    protected boolean setSocketOptions(Long socket) {
        if (socket == null || socket.longValue() == 0) {
            return false;
        }
        registerNativeConnection(socket.longValue());
        return true;
    }

    @Override
    protected void destroySocket(Long socket) {
        if (socket != null) {
            SocketWrapperBase<Long> wrapper = connections.remove(socket);
            if (wrapper != null) {
                wrapper.close();
            }
        }
    }

    /**
     * Tomcat-compatible processor using the exact SocketProcessorBase
     * dispatch boundary. Native transport I/O remains a separate gate.
     */
    protected class SocketProcessor extends SocketProcessorBase<Long> {

        public SocketProcessor(SocketWrapperBase<Long> socketWrapper, SocketEvent event) {
            super(socketWrapper, event);
        }

        @Override
        protected void doRun() {
            SocketWrapperBase<Long> wrapper = socketWrapper;
            if (wrapper == null || wrapper.isClosed()) {
                return;
            }

            Handler.SocketState state = getHandler().process(wrapper, event);
            if (state == Handler.SocketState.CLOSED) {
                wrapper.close();
            }
        }
    }
}
