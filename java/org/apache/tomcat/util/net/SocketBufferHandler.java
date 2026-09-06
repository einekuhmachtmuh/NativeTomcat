package org.apache.tomcat.util.net;
import java.nio.ByteBuffer;
public final class SocketBufferHandler {
    private final ByteBuffer readBuffer;
    private final ByteBuffer writeBuffer;
    public SocketBufferHandler(int readSize, int writeSize, boolean direct) {
        readBuffer = direct ? ByteBuffer.allocateDirect(readSize) : ByteBuffer.allocate(readSize);
        writeBuffer = direct ? ByteBuffer.allocateDirect(writeSize) : ByteBuffer.allocate(writeSize);
    }
    public ByteBuffer getReadBuffer() { return readBuffer; }
    public ByteBuffer getWriteBuffer() { return writeBuffer; }
    public boolean isWriteBufferEmpty() { return !writeBuffer.hasRemaining(); }
}
