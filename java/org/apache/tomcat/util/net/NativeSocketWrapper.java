package org.apache.tomcat.util.net;

import java.io.EOFException;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.CompletionHandler;
import java.nio.channels.ClosedChannelException;
import java.util.concurrent.Executor;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;

/**
 * Native transport SocketWrapperBase implementation.
 *
 * <p>The native runtime owns the file descriptor and readiness registration.
 * This wrapper owns Tomcat-side buffers, blocking wait state and the
 * transport-to-Tomcat I/O contract.</p>
 */
public final class NativeSocketWrapper extends SocketWrapperBase<Long> {
	private final Executor executor;
	private final Object readLock;
	private final Object writeLock;
	private volatile boolean readBlocking;
	private volatile boolean writeBlocking;
	private volatile ApplicationBufferHandler appReadBufHandler;

	public NativeSocketWrapper(long nativeHandle, AbstractEndpoint<Long, ?> endpoint)
	{
		super(nativeHandle, endpoint);
		this.executor = endpoint.getExecutor();
		this.socketBufferHandler = new SocketBufferHandler(16 * 1024, 16 * 1024, true);
		this.readLock = (readPending == null) ? new Object() : readPending;
		this.writeLock = (writePending == null) ? new Object() : writePending;
	}

	@Override
	protected void populateRemoteHost()
	{
		remoteHost = remoteAddr;
	}

	@Override
	protected void populateRemoteAddr()
	{
		remoteAddr = "native";
	}

	@Override
	protected void populateRemotePort()
	{
		remotePort = -1;
	}

	@Override
	protected void populateLocalName()
	{
		localName = localAddr;
	}

	@Override
	protected void populateLocalAddr()
	{
		localAddr = "native";
	}

	@Override
	protected void populateLocalPort()
	{
		localPort = -1;
	}

	@Override
	public boolean isReadyForRead() throws IOException
	{
		if (isClosed()) {
			return false;
		}

		socketBufferHandler.configureReadBufferForRead();
		if (socketBufferHandler.getReadBuffer().hasRemaining()) {
			return true;
		}

		int n = fillReadBuffer(false);
		return n > 0;
	}

	@Override
	public int read(boolean block, byte[] b, int off, int len) throws IOException
	{
		int nRead = populateReadBuffer(b, off, len);
		if (nRead > 0) {
			return nRead;
		}

		nRead = fillReadBuffer(block);
		updateLastRead();
		if (nRead > 0) {
			socketBufferHandler.configureReadBufferForRead();
			nRead = Math.min(nRead, len);
			socketBufferHandler.getReadBuffer().get(b, off, nRead);
		}
		return nRead;
	}

	@Override
	public int read(boolean block, ByteBuffer to) throws IOException
	{
		int nRead = populateReadBuffer(to);
		if (nRead > 0) {
			return nRead;
		}

		nRead = fillReadBuffer(block);
		updateLastRead();
		if (nRead > 0) {
			nRead = populateReadBuffer(to);
		}
		return nRead;
	}

	private int fillReadBuffer(boolean block) throws IOException
	{
		socketBufferHandler.configureReadBufferForWrite();
		ByteBuffer buffer = socketBufferHandler.getReadBuffer();
		return fillReadBuffer(block, buffer);
	}

	private int fillReadBuffer(boolean block, ByteBuffer buffer) throws IOException
	{
		if (isClosed()) {
			throw new ClosedChannelException();
		}

		if (!block) {
			int n = NativeTransport.read(getSocket(), buffer);
			if (n < 0) {
				throw new EOFException();
			}
			if (n > 0) {
				socketBufferHandler.configureReadBufferForRead();
			}
			return n;
		}

		long timeout = getReadTimeout();
		long startNanos = 0;
		int n;
		do {
			if (startNanos > 0) {
				long elapsedMillis = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - startNanos);
				if (elapsedMillis == 0) {
					elapsedMillis = 1;
				}
				timeout -= elapsedMillis;
				if (timeout <= 0) {
					throw new java.net.SocketTimeoutException();
				}
			}

			synchronized (readLock) {
				if (isClosed()) {
					throw new ClosedChannelException();
				}

				n = NativeTransport.read(getSocket(), buffer);
				if (n < 0) {
					throw new EOFException();
				}

				if (n == 0) {
					if (!readBlocking) {
						readBlocking = true;
						try {
							NativeTransport.rearm(getSocket(), false);
						} catch (IOException ioe) {
							readBlocking = false;
							throw ioe;
						}
					}
					try {
						if (timeout > 0) {
							startNanos = System.nanoTime();
							readLock.wait(timeout);
						} else {
							readLock.wait();
						}
					} catch (InterruptedException ignore) {
						// Re-check the socket state/readiness after interruption.
					}
				}
			}
		} while (n == 0);

		socketBufferHandler.configureReadBufferForRead();
		return n;
	}

	@Override
	protected void doClose()
	{
		synchronized (readLock) {
			readBlocking = false;
			readLock.notifyAll();
		}
		synchronized (writeLock) {
			writeBlocking = false;
			writeLock.notifyAll();
		}

		NativeTransport.close(getSocket());
		socketBufferHandler = SocketBufferHandler.EMPTY;
		nonBlockingWriteBuffer.clear();
	}

	@Override
	protected void doWrite(boolean block, ByteBuffer from) throws IOException
	{
		if (isClosed()) {
			throw new ClosedChannelException();
		}

		if (!block) {
			doNonBlockingWrite(from);
			updateLastWrite();
			return;
		}

		if (previousIOException != null) {
			throw new IOException(previousIOException);
		}

		long timeout = getWriteTimeout();
		long startNanos = 0;
		do {
			if (startNanos > 0) {
				long elapsedMillis = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - startNanos);
				if (elapsedMillis == 0) {
					elapsedMillis = 1;
				}
				timeout -= elapsedMillis;
				if (timeout <= 0) {
					previousIOException = new java.net.SocketTimeoutException();
					throw previousIOException;
				}
			}

			synchronized (writeLock) {
				if (isClosed()) {
					throw new ClosedChannelException();
				}

				int n = NativeTransport.write(getSocket(), from);
				if (n == 0 && from.hasRemaining()) {
					if (!writeBlocking) {
						writeBlocking = true;
						try {
							NativeTransport.rearm(getSocket(), true);
						} catch (IOException ioe) {
							writeBlocking = false;
							throw ioe;
						}
					}
					try {
						if (timeout > 0) {
							startNanos = System.nanoTime();
							writeLock.wait(timeout);
						} else {
							writeLock.wait();
						}
					} catch (InterruptedException ignore) {
						// Re-check the socket state/write readiness after interruption.
					}
				} else if (startNanos > 0) {
					timeout = getWriteTimeout();
					startNanos = 0;
				}
			}
		} while (from.hasRemaining());

		writeBlocking = false;
		updateLastWrite();
	}

	private void doNonBlockingWrite(ByteBuffer from) throws IOException
	{
		int n;
		do {
			n = NativeTransport.write(getSocket(), from);
		} while (n > 0 && from.hasRemaining());
	}

	@Override
	protected boolean flushNonBlocking() throws IOException
	{
		boolean dataLeft = !socketBufferHandler.isWriteBufferEmpty();
		if (dataLeft) {
			socketBufferHandler.configureWriteBufferForRead();
			doWrite(false, socketBufferHandler.getWriteBuffer());
			dataLeft = !socketBufferHandler.isWriteBufferEmpty();
		}

		if (!dataLeft && !nonBlockingWriteBuffer.isEmpty()) {
			dataLeft = nonBlockingWriteBuffer.write(this, false);
			if (!dataLeft && !socketBufferHandler.isWriteBufferEmpty()) {
				socketBufferHandler.configureWriteBufferForRead();
				doWrite(false, socketBufferHandler.getWriteBuffer());
				dataLeft = !socketBufferHandler.isWriteBufferEmpty();
			}
		}

		return dataLeft;
	}

	@Override
	public void registerReadInterest()
	{
		try {
			NativeTransport.rearm(getSocket(), false);
		} catch (IOException ioe) {
			setError(ioe);
			notifyReadReady();
		}
	}

	@Override
	public void registerWriteInterest()
	{
		try {
			NativeTransport.rearm(getSocket(), true);
		} catch (IOException ioe) {
			setError(ioe);
			notifyWriteReady();
		}
	}

	void notifyReadReady()
	{
		synchronized (readLock) {
			readBlocking = false;
			readLock.notify();
		}
	}

	void notifyWriteReady()
	{
		synchronized (writeLock) {
			writeBlocking = false;
			writeLock.notify();
		}
	}

	boolean isReadBlocking()
	{
		return readBlocking;
	}

	boolean isWriteBlocking()
	{
		return writeBlocking;
	}

	void processNativeEvent(int events)
	{
		if ((events & 0x8) != 0 || (events & 0x4) != 0 || ((events & 0x1) != 0 && readBlocking)) {
			notifyReadReady();
		}

		if ((events & 0x8) != 0 || ((events & 0x2) != 0 && writeBlocking)) {
			notifyWriteReady();
		}
	}

	@Override
	public void setAppReadBufHandler(ApplicationBufferHandler handler)
	{
		appReadBufHandler = handler;
	}

	public ApplicationBufferHandler getAppReadBufHandler()
	{
		return appReadBufHandler;
	}

	@Override
	public SendfileDataBase createSendfileData(String filename, long pos, long length)
	{
		throw new UnsupportedOperationException("sendfile is deferred");
	}

	@Override
	public SendfileState processSendfile(SendfileDataBase sendfileData)
	{
		throw new UnsupportedOperationException("sendfile is deferred");
	}

	@Override
	public void doClientAuth(SSLSupport sslSupport) throws IOException
	{
		throw new UnsupportedOperationException("TLS is deferred");
	}

	@Override
	public SSLSupport getSslSupport()
	{
		return null;
	}

	@Override
	protected <A> OperationState<A> newOperationState(boolean read, ByteBuffer[] buffers, int offset, int length,
			BlockingMode block, long timeout, TimeUnit unit, A attachment, CompletionCheck check,
			CompletionHandler<Long, ? super A> handler, Semaphore semaphore, VectoredIOCompletionHandler<A> completion)
	{
		throw new UnsupportedOperationException("vectored async I/O is deferred");
	}

	public Executor getNativeTestExecutor()
	{
		return executor;
	}
}
