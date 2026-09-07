package org.apache.coyote.http11;

import org.apache.juli.logging.Log;
import org.apache.juli.logging.LogFactory;
import org.apache.tomcat.util.net.NativeEndpoint;

/**
 * HTTP/1.1 protocol handler backed by the NativeTomcat endpoint.
 *
 * <p>The HTTP protocol implementation remains Tomcat's existing
 * {@link AbstractHttp11Protocol} / {@link Http11Processor} path. Only the
 * endpoint and socket type are changed at this stage; native transport I/O is
 * still a separate gate.</p>
 */
public class NativeHttp11Protocol extends AbstractHttp11Protocol<Long> {

    private static final Log log = LogFactory.getLog(NativeHttp11Protocol.class);

    /**
     * Construct a protocol handler with a native endpoint.
     */
    public NativeHttp11Protocol() {
        this(new NativeEndpoint());
    }

    /**
     * Construct a protocol handler using the supplied native endpoint.
     *
     * @param endpoint NativeTomcat endpoint
     */
    public NativeHttp11Protocol(NativeEndpoint endpoint) {
        super(endpoint);
    }

    /**
     * Return the native endpoint owned by this protocol handler.
     *
     * @return the native endpoint
     */
    public NativeEndpoint getNativeEndpoint() {
        return (NativeEndpoint) getEndpoint();
    }

    @Override
    protected Log getLog() {
        return log;
    }

    @Override
    protected String getNamePrefix() {
        return "http-native";
    }
}
