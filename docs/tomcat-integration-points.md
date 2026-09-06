# Tomcat 11.0.25 Integration Points

## 1. Verified request-processing path

The Tomcat 11.0.25 NIO path has a clear separation between readiness detection, socket-event dispatch, protocol processing and Servlet dispatch:

```text
NioEndpoint.Poller
    -> processKey()
    -> AbstractEndpoint.processSocket(..., dispatch=true)
    -> Executor.execute(SocketProcessor)
    -> SocketProcessorBase.run()
    -> endpoint-specific SocketProcessor.doRun()
    -> ProtocolHandler / Http11Processor
    -> Http11Processor.service()
    -> CoyoteAdapter.service()
    -> Catalina / FilterChain / Servlet
```

This mapping is based on the 11.0.25 source, not on a conceptual reconstruction.

## 2. Poller boundary

`NioEndpoint.Poller.processKey()` removes the ready operations from the selector registration before processing the event. For readable and writable events it may invoke `processSocket(socketWrapper, SocketEvent.OPEN_READ/OPEN_WRITE, true)`.

The important consequence is that the selector thread is not itself the Servlet execution thread. `processSocket()` creates or reuses a `SocketProcessor`, obtains the endpoint `Executor`, and when `dispatch` is true submits the processor to that executor. If there is no executor, it runs the processor directly.

Therefore the first native integration must not assume:

`epoll thread == Servlet thread`.

That assumption would be incompatible with the verified Tomcat execution model.

## 3. Per-connection serialization

`SocketProcessorBase.run()` acquires the `SocketWrapperBase` lock before invoking `doRun()`. It checks whether the wrapper is already closed and prevents simultaneous processing of read/write events for the same wrapper.

Therefore NativeTomcat must preserve an equivalent per-connection serialization rule if native readiness events can result in Java protocol processing. A native event loop may detect events concurrently, but it must not invoke Java operations in a way that permits conflicting processing of the same logical connection.

## 4. Socket wrapper ownership and lifecycle

`AbstractEndpoint` maintains the active connection map and `NioEndpoint.setSocketOptions()` creates the `NioChannel` and `NioSocketWrapper`, configures the socket as non-blocking, stores the wrapper in the connection map and registers it with the Poller.

`SocketWrapperBase.close()` uses an atomic closed flag and invokes the endpoint handler release path before connection accounting and the concrete `doClose()` operation. `NioSocketWrapper.doClose()` removes the connection, closes/resets the channel and may return the channel to the endpoint channel cache.

Therefore a NativeTomcat bridge must not simply free a native connection object when a Java callback returns. The lifetime has to cover pending event registration, Java processing and final close/recycle operations.

## 5. Read path

`SocketWrapperBase` exposes both byte-array and `ByteBuffer` read APIs. The NIO implementation first consumes data already present in the socket read buffer. For a sufficiently large destination `ByteBuffer`, `NioSocketWrapper.read(boolean, ByteBuffer)` can read directly into that destination; otherwise it reads through the socket buffer and transfers data into the destination.

This is important for the NativeTomcat design: direct `ByteBuffer` is already a first-class representation in Tomcat's socket layer. NativeTomcat should therefore avoid inventing a second incompatible buffer abstraction until differential measurements demonstrate a need.

## 6. Write path

`SocketWrapperBase.write(boolean, ByteBuffer)` is part of the existing transport abstraction. Non-blocking writes may leave data pending in the socket's non-blocking write buffer and trigger write-interest registration. NIO also has additional outbound buffering when TLS is enabled.

Consequently the first native write bridge must not equate `Java write()` with `send()` completion. The bridge needs an explicit pending-write state and must distinguish application bytes accepted into buffering from bytes actually transmitted to the peer.

## 7. HTTP parser boundary

`Http11Processor.service()` initializes the protocol I/O, parses the request line and headers, performs request preparation and then calls `getAdapter().service(request, response)`.

`prepareRequest()` performs security- and protocol-sensitive validation including Host handling, absolute URI handling, URI character validation and input-filter setup. This is a strong reason not to replace `Http11Processor` or `Http11InputBuffer` with a native parser before differential testing exists.

The initial NativeTomcat implementation therefore keeps HTTP semantic parsing in Tomcat Java.

## 8. Servlet boundary

`Http11Processor.service()` explicitly invokes the adapter after request preparation. This is the strongest verified semantic boundary for the first implementation: native transport should feed the existing Tomcat request-processing machinery rather than reimplementing Catalina or Servlet dispatch in C.

However, directly replacing the `SocketWrapperBase` implementation with a native object is not yet approved. `SocketWrapperBase` carries buffering, readiness, write-interest, close/recycle, timeout and locking semantics that must all be represented before such a replacement can be correct.

## 9. First implementation boundary

The current evidence supports the following staged boundary:

```text
C native runtime
    accept / epoll / connection state
            |
            | controlled JNI boundary
            v
Tomcat transport adapter
    SocketWrapperBase-compatible semantics
            |
            v
Http11Processor
    request parsing / validation / protocol state
            |
            v
CoyoteAdapter
            |
            v
Catalina / Servlet
```

The first native milestone should therefore be a **transport-adapter proof**, not a native HTTP parser.

## 10. Explicitly rejected assumptions

The source inspection does not support the following assumptions:

- the Poller thread executes Servlet code directly;
- a readable epoll/selector event means Servlet `isReady()` is true;
- a successful Java write means the peer has received the bytes;
- closing a socket immediately after a Java callback is safe;
- `SocketWrapperBase` is merely a thin socket descriptor wrapper;
- replacing `Http11InputBuffer` with a native parser is semantics-neutral;
- JNI is inherently faster than FFM.

Each of these would require additional source verification or measurement before being used as an implementation premise.

## 11. Next gate

Before creating the first Java JNI class, verify the exact `SocketWrapperBase` abstract/virtual surface required by `Http11Processor`, including:

- read/write operations;
- readiness and write-interest registration;
- close/recycle;
- timeout state;
- per-connection lock;
- application buffer handler;
- async/non-blocking operations;
- SSL support hooks;
- sendfile and upgrade paths.

Only after this surface is enumerated should the repository define the first concrete Java transport adapter.
