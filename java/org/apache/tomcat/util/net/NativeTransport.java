package org.apache.tomcat.util.net;

import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * Minimal native transport bridge for the first real SocketWrapperBase I/O gate.
 * The native event loop remains the owner of socket interest and connection lifetime.
 */
final class NativeTransport {

    private NativeTransport() {
    }

    static native int read(long handle, ByteBuffer buffer) throws IOException;

    static native int write(long handle, ByteBuffer buffer) throws IOException;

    static native void rearm(long handle, boolean wantWrite) throws IOException;

    static native void close(long handle);
}
