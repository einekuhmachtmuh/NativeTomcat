# Upstream source mapping — initial pass

## Verified baseline

### Apache Tomcat 11.0.25

Apache's official download/documentation pages identify 11.0.25 as the current Tomcat 11.0.x release at the time of this analysis. Tomcat 11.0.25 implements Jakarta Servlet 6.1. The Tomcat build documentation states that building 11.0.25 requires JDK 22 or later and Apache Ant 1.10.2 or later.

Primary source: https://tomcat.apache.org/tomcat-11.0-doc/building.html

Git source ref: `11.0.25`

### NGINX 1.30.4

The official NGINX download page identifies 1.30.4 as the stable release. The source repository ref used for source inspection is `release-1.30.4`.

Primary source: https://nginx.org/en/download.html

## Tomcat request-path facts verified from source

### NioEndpoint

`org.apache.tomcat.util.net.NioEndpoint` is a Java NIO network endpoint built on `ServerSocketChannel`, `SocketChannel`, `Selector`, `ByteBuffer`, and Tomcat's endpoint/poller abstractions. It also contains channel and buffer caches and socket lifecycle state.

Source: https://github.com/apache/tomcat/blob/11.0.25/java/org/apache/tomcat/util/net/NioEndpoint.java

### Http11Processor

`org.apache.coyote.http11.Http11Processor` owns HTTP/1.1 request/response processing and has explicit input/output buffer objects, an HTTP parser, keep-alive state, content delimitation state, and upgrade/sendfile state. Its constructor obtains the protocol parser and creates the HTTP input/output buffers.

Source: https://github.com/apache/tomcat/blob/11.0.25/java/org/apache/coyote/http11/Http11Processor.java

### CoyoteAdapter

`org.apache.catalina.connector.CoyoteAdapter` implements the Coyote-to-Catalina adapter. The source contains explicit async dispatch handling, Servlet `ReadListener`/`WriteListener` interaction, request/response note objects, context class-loader binding, error propagation and socket-close actions. This makes it a critical Java semantic boundary and not an obvious candidate for wholesale C translation.

Source: https://github.com/apache/tomcat/blob/11.0.25/java/org/apache/catalina/connector/CoyoteAdapter.java

## NGINX event facts verified from source

### Event core

`src/event/ngx_event.c` defines event configuration and state including worker connection capacity, event backend selection, multi-accept, accept mutex configuration, and connection counters. The source also references platform event modules such as epoll and kqueue.

Source: https://github.com/nginx/nginx/blob/release-1.30.4/src/event/ngx_event.c

### Posted events

`src/event/ngx_event_posted.c` maintains posted-event queues and processes queued handlers through `ngx_event_process_posted()`. This is relevant to a NativeTomcat event-loop design because readiness notification and deferred handler execution should be considered separately.

Source: https://github.com/nginx/nginx/blob/release-1.30.4/src/event/ngx_event_posted.c

## Servlet 6.1 constraint

The Servlet API documentation defines the `Servlet` lifecycle (`init`, `service`, `destroy`) and the container/application contract. Therefore the first native prototype must not claim Servlet compatibility merely because it can accept TCP connections: compatibility begins only after the Java Servlet lifecycle, request/response contract, mapping and error semantics are integrated and tested.

Primary source: https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/Servlet.html

## Current implementation boundary

The initial native code is deliberately only a runtime scaffold: listener creation, non-blocking accept and an epoll wait loop. It does not yet implement connection state, HTTP parsing, request/response bridging, Servlet dispatch, keep-alive, TLS, back-pressure or Java integration. It therefore makes no compatibility or performance claim.

## Important limitation

The coding environment's local shell cannot resolve GitHub, so upstream repositories cannot currently be cloned into the local filesystem. Repository writes are available through the GitHub integration, and selected upstream files have been verified through the GitHub source interface. Full Ant compilation, integration testing and benchmark execution require a build host with the upstream Tomcat source tree and dependencies available.
