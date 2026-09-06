package org.apache.tomcat.util.net;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.concurrent.CompletionException;
import java.util.concurrent.Executor;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.nio.channels.CompletionHandler;

/**
 * First concrete NativeTomcat SocketWrapperBase surface proof.
 * Transport I/O is intentionally not implemented yet; unsupported features
 * remain explicit until their Tomcat 11.0.25 semantics are migrated.
 */
public final class NativeSocketWrapper extends SocketWrapperBase<Long> {
    private final Executor executor;
    private volatile boolean readInterest;
    private volatile boolean writeInterest;
    private volatile ApplicationBufferHandler appReadBufHandler;

    public NativeSocketWrapper(long nativeHandle, AbstractEndpoint<Long, ?> endpoint) {
        super(nativeHandle, endpoint);
        this.executor = endpoint.getExecutor();
        this.socketBufferHandler = new SocketBufferHandler(16 * 1024, 16 * 1024, true);
    }

    @Override protected void populateRemoteHost() { remoteHost = remoteAddr; }
    @Override protected void populateRemoteAddr() { remoteAddr = "native"; }
    @Override protected void populateRemotePort() { remotePort = -1; }
    @Override protected void populateLocalName() { localName = localAddr; }
    @Override protected void populateLocalAddr() { localAddr = "native"; }
    @Override protected void populateLocalPort() { localPort = -1; }

    @Override public int read(boolean block, byte[] b, int off, int len) throws IOException {
        throw new UnsupportedOperationException("native read bridge not implemented in surface proof");
    }
    @Override public int read(boolean block, ByteBuffer to) throws IOException {
        throw new UnsupportedOperationException("native ByteBuffer read bridge not implemented in surface proof");
    }
    @Override public boolean isReadyForRead() { return !isClosed(); }
    @Override public void setAppReadBufHandler(ApplicationBufferHandler handler) { appReadBufHandler = handler; }
    public ApplicationBufferHandler getAppReadBufHandler() { return appReadBufHandler; }
    @Override protected void doClose() { /* Native handle release is the next lifecycle gate. */ }
    @Override protected void doWrite(boolean block, ByteBuffer from) throws IOException {
        throw new UnsupportedOperationException("native write bridge not implemented in surface proof");
    }
    @Override protected boolean flushNonBlocking() { return hasDataToWrite(); }
    @Override public void registerReadInterest() { readInterest = true; }
    @Override public void registerWriteInterest() { writeInterest = true; }
    public boolean hasReadInterest() { return readInterest; }
    public boolean hasWriteInterest() { return writeInterest; }
    @Override public SendfileDataBase createSendfileData(String filename, long pos, long length) {
        throw new UnsupportedOperationException("sendfile is deferred");
    }
    @Override public SendfileState processSendfile(SendfileDataBase sendfileData) {
        throw new UnsupportedOperationException("sendfile is deferred");
    }
    @Override public void doClientAuth(SSLSupport sslSupport) throws IOException {
        throw new UnsupportedOperationException("TLS is deferred");
    }
    @Override public SSLSupport getSslSupport() { return null; }
    @Override protected <A> OperationState<A> newOperationState(boolean read, ByteBuffer[] buffers, int offset, int length,
            BlockingMode block, long timeout, TimeUnit unit, A attachment, CompletionCheck check,
            CompletionHandler<Long, ? super A> handler, Semaphore semaphore, VectoredIOCompletionHandler<A> completion) {
        throw new UnsupportedOperationException("vectored async I/O is deferred");
    }

    public Executor getNativeTestExecutor() { return executor; }
}
