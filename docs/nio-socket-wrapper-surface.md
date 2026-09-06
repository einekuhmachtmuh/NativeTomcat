# Tomcat 11.0.25 NIO Socket Wrapper Surface

## 1. Verification scope

This document records the exact transport-facing behavior inspected in the pinned Tomcat 11.0.25 source before implementing a native-backed adapter.

Primary sources:

- `org/apache/tomcat/util/net/NioEndpoint.java`
- `org/apache/tomcat/util/net/SocketWrapperBase.java`

This is an implementation mapping, not a Servlet specification. Servlet semantics remain a separate compatibility layer.

## 2. Connection creation path

The verified NIO path is:

```text
Acceptor
  -> AbstractEndpoint.setSocketOptions()
  -> NioEndpoint.setSocketOptions(SocketChannel)
  -> obtain/create NioChannel + SocketBufferHandler
  -> new NioSocketWrapper(channel, endpoint)
  -> channel.reset(socket, wrapper)
  -> connections.put(socket, wrapper)
  -> socket.configureBlocking(false)
  -> timeout / keep-alive configuration
  -> poller.register(wrapper)
```

The wrapper is therefore created together with Tomcat's channel/buffer abstraction; it is not merely a wrapper around an integer file descriptor. fileciteturn143file0L2-L2

## 3. Poller/event path

`NioEndpoint.Poller` owns a Java NIO `Selector`, an event queue, a wakeup counter and a close flag. Registration first arms `OP_READ`. Readable and writable keys are removed from the current interest set before processing so multiple threads do not concurrently manipulate the same readiness event. Read and write events are then converted into `SocketEvent.OPEN_READ` / `OPEN_WRITE` processor dispatch unless a blocking or asynchronous operation owns that event. fileciteturn143file0L2-L2

Timeout processing can convert an expired read/write condition into `SocketEvent.ERROR` after recording a `SocketTimeoutException`; closure is performed when the processor cannot continue. fileciteturn144file0L2-L2

## 4. Verified NioSocketWrapper surface

| Method / state | Verified behavior | Native adapter implication |
|---|---|---|
| `interestOps()` | Per-wrapper interest bit mask | Native event registration must have an equivalent state and avoid lost/duplicate interest |
| `setSendfileData()` / `getSendfileData()` | Sendfile state is stored on wrapper | Deferred initially; never claim sendfile support until implemented |
| `isReadyForRead()` | Checks Tomcat read buffer first, then attempts to fill it | Native readable readiness cannot simply be returned as Java readiness |
| `read(boolean, byte[], int, int)` | Consumes buffered bytes first, then fills from socket | Preserve buffering and EOF/error behavior |
| `read(boolean, ByteBuffer)` | May read directly into caller buffer when sufficiently large; otherwise uses socket buffer | This is the strongest initial direct-buffer optimization point |
| `doClose()` | Removes connection, closes channel, returns/free channel, clears buffers, resets wrapper | Native lifetime must coordinate with Java wrapper closure |
| `flushNonBlocking()` | Drains socket/network buffers and Tomcat non-blocking write buffer | Native pending-write state must preserve completion semantics |
| `hasDataToWrite()` | Includes Tomcat buffers and TLS outbound data | Plain TCP adapter can omit TLS-specific state only while TLS is explicitly unsupported |
| `canWrite()` | Requires writable Tomcat buffer and no TLS outbound remainder | Transport writable event is not equivalent to Servlet output readiness |
| `doWrite(boolean, ByteBuffer)` | Blocking writes may wait and enforce timeout; non-blocking writes continue until socket would stop accepting data | Preserve partial-write semantics and timeout/error transitions |
| `registerReadInterest()` | Adds `OP_READ` through the Poller | Native equivalent must be edge/level semantics compatible with the Java state machine |
| `registerWriteInterest()` | Adds `OP_WRITE` through the Poller | Must retain pending output until drained |
| `processSendfile()` | Uses the Poller's sendfile processing path | Deferred initially |
| remote/local metadata | Populates address/host/port from the channel | Native connection creation can provide equivalent values, but Java-visible caching must remain coherent |
| TLS methods | `getSslSupport()` and client-auth behavior depend on `SecureNioChannel` | Deferred in first plain-TCP milestone |
| async operation state | NIO operation state supports completion handlers and blocking modes | Deferred; do not expose incomplete async API |

The read/write/interest/close methods above are directly present in the pinned NIO implementation. fileciteturn144file0L2-L2 fileciteturn145file0L2-L2

## 5. SocketWrapperBase obligations

`SocketWrapperBase` adds obligations that a native transport must preserve even though they are not themselves native I/O calls:

- one `ReentrantLock` for connection-level processor serialization;
- atomic closed state and idempotent `close()`;
- read/write timeout values;
- first-error recording and later `checkError()`;
- current-processor association;
- local/remote metadata caches;
- internal socket read/write buffers;
- an additional non-blocking `WriteBuffer` for data that cannot fit in the socket buffer;
- pending read/write operation state and semaphores when async I/O is enabled. fileciteturn146file0L2-L2

The base class also explicitly defines `isReadyForWrite()` in terms of `canWrite()` and `registerWriteInterest()`, and defines the abstract read methods plus `isReadyForRead()`. fileciteturn147file0L2-L2

## 6. Critical buffer consequence

The NIO implementation can avoid an intermediate copy when the caller's `ByteBuffer` has enough remaining capacity: it changes the destination limit, reads directly into that destination, then returns the number of bytes read. Otherwise it fills the internal socket read buffer and transfers data into the caller buffer. fileciteturn144file0L2-L2

This validates the project decision to make direct `ByteBuffer` bulk transfer the first JNI optimization target. It does **not** justify removing Tomcat's buffering wholesale.

## 7. Critical write consequence

A non-blocking write can leave data in Tomcat's socket buffer or `WriteBuffer`; `hasDataToWrite()` and `flushNonBlocking()` account for pending data. The native adapter must therefore represent at least:

```text
Java/Tomcat pending output
        |
        v
native pending output
        |
        v
kernel socket
```

A successful Java-side write call cannot be interpreted as peer receipt.

## 8. Blocking and non-blocking distinction

The NIO implementation uses separate blocking and non-blocking paths. Blocking reads/writes can wait on per-connection locks and register readiness before waiting. Non-blocking operations return without waiting and rely on readiness registration for later progress. fileciteturn145file0L2-L2

The first native adapter should therefore preserve the distinction rather than expose one generic `native_read()` / `native_write()` operation and infer semantics later.

## 9. Native event-loop mapping

The NativeTomcat mapping is now sufficiently constrained to:

```text
Native epoll
    |
    +-- readable ----> Java transport event OPEN_READ
    |
    +-- writable ----> Java transport event OPEN_WRITE
    |
    +-- error/timeout -> Java ERROR / close path
    |
    +-- shutdown ----> Java STOP / close path
```

The native event loop must not invoke `Servlet.service()` directly. The Java side retains the Tomcat processor, HTTP parser, Adapter, Catalina pipeline and Servlet lifecycle.

## 10. Remaining implementation boundary

The next code step is **not** a native HTTP parser. It is a minimal JNI transport adapter with:

1. an opaque native connection handle;
2. creation/destruction and idempotent close;
3. plain TCP non-blocking read/write;
4. direct `ByteBuffer` bulk transfer;
5. read/write interest registration;
6. Java-visible error/EOF transitions;
7. explicit native-to-Java event dispatch;
8. shutdown-safe lifetime tracking.

TLS, sendfile, HTTP/2, NIO2/vectored async I/O and native HTTP parsing remain deferred.

## 11. Verification status

The upstream source surface above has been inspected from Tomcat 11.0.25. This does **not** mean the NativeTomcat adapter has been compiled or run. The local environment still lacks a checkout of the current repository and cannot resolve GitHub from the shell, so no current-repository build result is claimed.
