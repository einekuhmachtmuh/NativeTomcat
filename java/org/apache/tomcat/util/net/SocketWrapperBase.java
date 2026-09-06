/*
 * NativeTomcat intermediate compatibility shell.
 *
 * This file is NOT a replacement copy of Tomcat's SocketWrapperBase.java.
 * Its public/protected surface is derived from Apache Tomcat 11.0.25 and is
 * intentionally kept small enough to verify the native transport boundary
 * while the upstream implementation is being migrated into this repository.
 */
package org.apache.tomcat.util.net;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.CompletionHandler;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.Semaphore;
import java.util.concurrent.locks.ReentrantLock;

/** Tomcat 11.0.25 SocketWrapperBase compatibility surface for NativeTomcat. */
public abstract class SocketWrapperBase<E> {
    private static final AtomicLong IDS = new AtomicLong();
    private E socket;
    private final AbstractEndpoint<E, ?> endpoint;
    private final ReentrantLock lock = new ReentrantLock();
    protected final AtomicBoolean closed = new AtomicBoolean(false);
    protected volatile IOException previousIOException;
    protected volatile SocketBufferHandler socketBufferHandler;
    protected int bufferedWriteSize = 64 * 1024;
    protected final WriteBuffer nonBlockingWriteBuffer = new WriteBuffer(bufferedWriteSize);
    protected final Semaphore readPending = new Semaphore(1);
    protected final Semaphore writePending = new Semaphore(1);
    protected volatile OperationState<?> readOperation;
    protected volatile OperationState<?> writeOperation;
    protected String localAddr, localName, remoteAddr, remoteHost, sniHostName;
    protected int localPort = -1, remotePort = -1;
    protected volatile Object servletConnection;
    private volatile long readTimeout = -1, writeTimeout = -1;
    private volatile int keepAliveLeft = 100;
    private volatile String negotiatedProtocol;
    private volatile IOException error;
    private final AtomicReference<Object> currentProcessor = new AtomicReference<>();
    private final String connectionId = Long.toHexString(IDS.getAndIncrement());

    protected SocketWrapperBase(E socket, AbstractEndpoint<E, ?> endpoint) {
        this.socket = socket;
        this.endpoint = endpoint;
    }

    public E getSocket() { return socket; }
    protected void reset(E closedSocket) { socket = closedSocket; }
    protected AbstractEndpoint<E, ?> getEndpoint() { return endpoint; }
    public ReentrantLock getLock() { return lock; }
    public Object getCurrentProcessor() { return currentProcessor.get(); }
    public void setCurrentProcessor(Object p) { currentProcessor.set(p); }
    public Object takeCurrentProcessor() { return currentProcessor.getAndSet(null); }
    public void execute(Runnable runnable) { endpoint.execute(runnable); }
    public IOException getError() { return error; }
    public void setError(IOException e) { if (error == null) error = e; }
    public void checkError() throws IOException { if (error != null) throw error; }
    public String getNegotiatedProtocol() { return negotiatedProtocol; }
    public void setNegotiatedProtocol(String p) { negotiatedProtocol = p; }
    public String getSniHostName() { return sniHostName; }
    public void setSniHostName(String s) { sniHostName = s; }
    public void setReadTimeout(long t) { readTimeout = t > 0 ? t : -1; }
    public long getReadTimeout() { return readTimeout; }
    public void setWriteTimeout(long t) { writeTimeout = t > 0 ? t : -1; }
    public long getWriteTimeout() { return writeTimeout; }
    public void setKeepAliveLeft(int n) { keepAliveLeft = n; }
    public int decrementKeepAlive() { return --keepAliveLeft; }
    public String getRemoteHost() { if (remoteHost == null) populateRemoteHost(); return remoteHost; }
    public String getRemoteAddr() { if (remoteAddr == null) populateRemoteAddr(); return remoteAddr; }
    public int getRemotePort() { if (remotePort < 0) populateRemotePort(); return remotePort; }
    public String getLocalName() { if (localName == null) populateLocalName(); return localName; }
    public String getLocalAddr() { if (localAddr == null) populateLocalAddr(); return localAddr; }
    public int getLocalPort() { if (localPort < 0) populateLocalPort(); return localPort; }
    public SocketBufferHandler getSocketBufferHandler() { return socketBufferHandler; }
    public boolean hasDataToRead() { return true; }
    public boolean hasDataToWrite() { return !nonBlockingWriteBuffer.isEmpty(); }
    public boolean isReadyForWrite() { return canWrite(); }
    public boolean canWrite() { return !closed.get() && !hasDataToWrite(); }
    public boolean hasAsyncIO() { return false; }
    public boolean hasPerOperationTimeout() { return false; }
    public boolean isReadPending() { return readOperation != null; }
    public boolean isWritePending() { return writeOperation != null; }
    public boolean isClosed() { return closed.get(); }

    public void close() {
        if (closed.compareAndSet(false, true)) {
            doClose();
        }
    }

    public void processSocket(SocketEvent socketStatus, boolean dispatch) {
        endpoint.processSocket(this, socketStatus, dispatch);
    }

    public final void write(boolean block, byte[] buf, int off, int len) throws IOException {
        write(block, ByteBuffer.wrap(buf, off, len));
    }

    public final void write(boolean block, ByteBuffer from) throws IOException {
        doWrite(block, from);
    }

    protected void writeBlocking(byte[] buf, int off, int len) throws IOException { doWrite(true, ByteBuffer.wrap(buf, off, len)); }
    protected void writeBlocking(ByteBuffer from) throws IOException { doWrite(true, from); }
    protected void writeNonBlocking(byte[] buf, int off, int len) throws IOException { doWrite(false, ByteBuffer.wrap(buf, off, len)); }
    protected void writeNonBlocking(ByteBuffer from) throws IOException { doWrite(false, from); }
    protected void writeNonBlockingInternal(ByteBuffer from) throws IOException { doWrite(false, from); }
    protected void doWrite(boolean block) throws IOException { if (socketBufferHandler != null) doWrite(block, socketBufferHandler.getWriteBuffer()); }
    public boolean flush(boolean block) throws IOException { return flushNonBlocking(); }
    protected void flushBlocking() throws IOException { while (flushNonBlocking()) { Thread.yield(); } }

    public void unRead(ByteBuffer returnedInput) { throw new UnsupportedOperationException("push-back not implemented in initial NativeSocketWrapper"); }
    public ServletConnectionFacade getServletConnection(String protocol, String protocolConnectionId) { return new ServletConnectionFacade(connectionId, protocol, protocolConnectionId); }
    public String toString() { return "NativeSocketWrapper[" + connectionId + "]"; }

    public enum BlockingMode { BLOCK, SEMI_BLOCK, NON_BLOCK }
    public enum CompletionState { DONE, DONE_INLINE, PENDING }
    public enum CompletionHandlerCall { NONE, DONE, DONE_INLINE, CONTINUE }
    public interface CompletionCheck { CompletionHandlerCall callHandler(CompletionState state, long nBytes); }
    protected class OperationState<A> { }
    protected class VectoredIOCompletionHandler<A> { }

    public static final CompletionCheck COMPLETE_WRITE = (state, n) -> CompletionHandlerCall.DONE;
    public static final CompletionCheck COMPLETE_WRITE_WITH_COMPLETION = COMPLETE_WRITE;
    public static final CompletionCheck READ_DATA = (state, n) -> CompletionHandlerCall.DONE;
    public static final CompletionCheck COMPLETE_READ_WITH_COMPLETION = READ_DATA;
    public static final CompletionCheck COMPLETE_READ = READ_DATA;

    public final <A> CompletionState read(long timeout, TimeUnit unit, A attachment,
            CompletionHandler<Long, ? super A> handler, ByteBuffer... dsts) {
        return read(BlockingMode.SEMI_BLOCK, timeout, unit, attachment, READ_DATA, handler, dsts);
    }
    public final <A> CompletionState read(BlockingMode block, long timeout, TimeUnit unit, A attachment,
            CompletionCheck check, CompletionHandler<Long, ? super A> handler, ByteBuffer... dsts) {
        return vectoredOperation(true, dsts, 0, dsts.length, block, timeout, unit, attachment, check, handler);
    }
    public final <A> CompletionState read(ByteBuffer[] dsts, int offset, int length, BlockingMode block,
            long timeout, TimeUnit unit, A attachment, CompletionCheck check,
            CompletionHandler<Long, ? super A> handler) {
        return vectoredOperation(true, dsts, offset, length, block, timeout, unit, attachment, check, handler);
    }
    public final <A> CompletionState write(long timeout, TimeUnit unit, A attachment,
            CompletionHandler<Long, ? super A> handler, ByteBuffer... srcs) {
        return write(BlockingMode.SEMI_BLOCK, timeout, unit, attachment, COMPLETE_WRITE, handler, srcs);
    }
    public final <A> CompletionState write(BlockingMode block, long timeout, TimeUnit unit, A attachment,
            CompletionCheck check, CompletionHandler<Long, ? super A> handler, ByteBuffer... srcs) {
        return vectoredOperation(false, srcs, 0, srcs.length, block, timeout, unit, attachment, check, handler);
    }
    public final <A> CompletionState write(ByteBuffer[] srcs, int offset, int length, BlockingMode block,
            long timeout, TimeUnit unit, A attachment, CompletionCheck check,
            CompletionHandler<Long, ? super A> handler) {
        return vectoredOperation(false, srcs, offset, length, block, timeout, unit, attachment, check, handler);
    }
    protected final <A> CompletionState vectoredOperation(boolean read, ByteBuffer[] buffers, int offset, int length,
            BlockingMode block, long timeout, TimeUnit unit, A attachment, CompletionCheck check,
            CompletionHandler<Long, ? super A> handler) {
        throw new UnsupportedOperationException("vectored I/O is explicitly deferred");
    }

    protected abstract void populateRemoteHost();
    protected abstract void populateRemoteAddr();
    protected abstract void populateRemotePort();
    protected abstract void populateLocalName();
    protected abstract void populateLocalAddr();
    protected abstract void populateLocalPort();
    public abstract int read(boolean block, byte[] b, int off, int len) throws IOException;
    public abstract int read(boolean block, ByteBuffer to) throws IOException;
    public abstract boolean isReadyForRead() throws IOException;
    public abstract void setAppReadBufHandler(ApplicationBufferHandler handler);
    protected abstract void doClose();
    protected abstract void doWrite(boolean block, ByteBuffer from) throws IOException;
    protected abstract boolean flushNonBlocking() throws IOException;
    public abstract void registerReadInterest();
    public abstract void registerWriteInterest();
    public abstract SendfileDataBase createSendfileData(String filename, long pos, long length);
    public abstract SendfileState processSendfile(SendfileDataBase sendfileData);
    public abstract void doClientAuth(SSLSupport sslSupport) throws IOException;
    public abstract SSLSupport getSslSupport();
    protected abstract <A> OperationState<A> newOperationState(boolean read, ByteBuffer[] buffers, int offset, int length,
            BlockingMode block, long timeout, TimeUnit unit, A attachment, CompletionCheck check,
            CompletionHandler<Long, ? super A> handler, Semaphore semaphore, VectoredIOCompletionHandler<A> completion);
}
