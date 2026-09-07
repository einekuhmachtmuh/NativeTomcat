package org.apache.tomcat.util.net;

import java.io.IOException;
import java.net.InetSocketAddress;

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
	public NativeEndpoint()
	{
		// Do not create an Acceptor here. NioEndpoint creates and starts its
		// Acceptor as part of its own startInternal() implementation. Native
		// accept is owned exclusively by nt_runtime, so this endpoint must not
		// create a second accept owner.
	}

	/**
	 * Register a native connection with the Tomcat-side connection registry.
	 *
	 * @param nativeHandle Stable native connection handle
	 * @return The existing or newly created wrapper
	 */
	public NativeSocketWrapper registerNativeConnection(long nativeHandle)
	{
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
	 * <p>Tomcat's NioEndpoint gives a blocking reader/writer precedence over
	 * SocketProcessor dispatch: the poller wakes the waiter and only dispatches
	 * an OPEN_READ/OPEN_WRITE processor when no corresponding blocking I/O is
	 * waiting. NativeEndpoint preserves that distinction for native readiness.</p>
	 *
	 * @param nativeHandle Stable native connection handle
	 * @param events Native runtime event mask
	 * @return {@code true} if processing was submitted successfully
	 */
	public boolean processNativeEvent(long nativeHandle, int events)
	{
		NativeSocketWrapper wrapper = registerNativeConnection(nativeHandle);
		boolean readBlocking = wrapper.isReadBlocking();
		boolean writeBlocking = wrapper.isWriteBlocking();

		wrapper.processNativeEvent(events);

		if ((events & 0x8) != 0) {
			if (readBlocking || writeBlocking) {
				return true;
			}
			return processSocket(wrapper, SocketEvent.ERROR, false);
		}

		if ((events & 0x1) != 0 || (events & 0x4) != 0) {
			if (!readBlocking && !processSocket(wrapper, SocketEvent.OPEN_READ, false)) {
				return false;
			}
		}

		if ((events & 0x2) != 0 && !writeBlocking) {
			return processSocket(wrapper, SocketEvent.OPEN_WRITE, false);
		}

		return true;
	}

	@Override
	protected SocketProcessorBase<Long> createSocketProcessor(SocketWrapperBase<Long> socketWrapper, SocketEvent event)
	{
		return new SocketProcessor(socketWrapper, event);
	}

	@Override
	public void bind()
	{
		// The native runtime owns the listening socket. NativeEndpoint is only
		// the Tomcat-side endpoint contract at this stage.
	}

	@Override
	public void startInternal()
	{
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
	public void stopInternal()
	{
		if (running) {
			running = false;
			paused = true;
			shutdownExecutor();
			if (processorCache != null) {
				processorCache.clear();
				processorCache = null;
			}
		}
	}

	@Override
	protected Log getLog()
	{
		return log;
	}

	@Override
	protected InetSocketAddress getLocalAddress() throws IOException
	{
		// The listening socket is owned by nt_runtime. Until the native
		// listener-address bridge is added, there is no Java NetworkChannel
		// from which AbstractEndpoint can obtain an address.
		return null;
	}

	@Override
	protected void doCloseServerSocket() throws IOException
	{
		// No Java server socket exists. Native runtime owns this resource.
	}

	@Override
	protected Long serverSocketAccept()
	{
		throw new UnsupportedOperationException("native runtime owns accept");
	}

	@Override
	protected boolean setSocketOptions(Long socket)
	{
		if (socket == null || socket.longValue() == 0) {
			return false;
		}
		registerNativeConnection(socket.longValue());
		return true;
	}

	@Override
	protected void destroySocket(Long socket)
	{
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

		public SocketProcessor(SocketWrapperBase<Long> socketWrapper, SocketEvent event)
		{
			super(socketWrapper, event);
		}

		@Override
		protected void doRun()
		{
			try {
				SocketWrapperBase<Long> wrapper = socketWrapper;
				if (wrapper == null || wrapper.isClosed()) {
					return;
				}

				Handler.SocketState state = getHandler().process(wrapper, event);
				if (state == Handler.SocketState.CLOSED) {
					wrapper.close();
				}
			} finally {
				event = null;
				if (running && processorCache != null) {
					processorCache.push(this);
				}
			}
		}
	}
}
