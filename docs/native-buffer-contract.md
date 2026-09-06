# Native buffer contract

## 1. Purpose and status

This document defines the first C/Java transport-buffer contract for NativeTomcat before JNI request/response I/O is implemented.

It is intentionally narrower than a Servlet implementation contract. It defines how native socket I/O interacts with Java/Tomcat buffers; it does not define Servlet request/response semantics, HTTP parsing, TLS, or the final JNI class layout.

The contract distinguishes three categories:

- **Verified upstream/spec fact**: behavior required by Jakarta Servlet 6.1, Java/JNI, or the inspected Tomcat 11.0.25 source.
- **NativeTomcat design decision**: the chosen implementation rule for the first native transport path.
- **Deferred**: behavior that must not be assumed until a later source-level implementation and differential test establish it.

## 2. Verified constraints

Jakarta Servlet 6.1 adds `ByteBuffer` operations to `ServletInputStream` and `ServletOutputStream`. For `ServletInputStream.read(ByteBuffer)`, the Servlet 6.1 API specifies that a successful read leaves the buffer position unchanged and sets the limit to the old position plus the number of bytes read. A zero-remaining buffer returns `0` without modification, and EOF without data returns `-1`. Non-blocking reads additionally require the Servlet readiness/listener rules to have been satisfied. citeturn0search0

For `ServletOutputStream.write(ByteBuffer)`, Servlet 6.1 requires non-blocking readiness checks. In blocking mode, when data is written, the buffer position reaches the original limit. In non-blocking mode, the buffer state must not be modified until the stream becomes writable again; then the position reaches the unchanged limit. citeturn1search5

Tomcat 11.0.25's `Http11InputBuffer` owns an incremental `ByteBuffer` and uses `SocketWrapperBase.read(...)` to fill it. Its parser explicitly maintains state so incomplete network input can be resumed when more data arrives. Therefore the native transport must not assume that one socket read corresponds to one HTTP request or header. citeturn0search2turn0search9

Java NIO distinguishes direct and non-direct `ByteBuffer`. Direct buffers are intended to permit best-effort avoidance of intermediate copies for native I/O, but direct allocation has different allocation/deallocation costs and should not be assumed to be universally preferable. citeturn1search1

JNI provides `GetDirectBufferAddress` and `GetDirectBufferCapacity` for direct buffers. A JVM may return `NULL` / `-1` when JNI direct-buffer access is unsupported, so NativeTomcat must not treat successful direct-buffer address acquisition as a universal JVM guarantee. citeturn1search0

## 3. Ownership model for the first implementation

### 3.1 Socket and event state

**NativeTomcat owns:**

- accepted socket file descriptor;
- native connection state;
- epoll registration and rearm state;
- native event-loop ownership;
- native error/EOF observation.

Java does not receive or close the native file descriptor directly.

### 3.2 Tomcat parser buffers

**Tomcat/Java owns:**

- `Http11InputBuffer.byteBuffer`;
- `SocketBufferHandler` buffers;
- Java `ByteBuffer` objects and their `position`, `limit`, `capacity`, and mark state;
- Servlet-visible `ByteBuffer` objects.

The first transport integration therefore uses a **borrowed Java buffer**, not a second permanent C request buffer.

The C layer receives access to a Java-owned destination/source only for the duration of a single native I/O operation. C must not retain its address after that call returns.

This is a deliberate refinement of the earlier generic C/Java boundary document: the first implementation avoids introducing a second persistent native input buffer before differential testing demonstrates that such duplication is beneficial.

### 3.3 Temporary native scratch storage

A temporary C scratch buffer is permitted only for a compatibility fallback when the supplied Java `ByteBuffer` cannot be accessed directly from native code.

Such scratch storage is operation-scoped. It must be copied back to the Java buffer before the JNI call returns, and C must not retain it as request state.

A persistent native buffer pool is deferred.

## 4. Read operation contract

The conceptual native operation is:

`readInto(javaByteBuffer)`

The Java adapter is responsible for establishing the exact buffer window passed to native code. Native code operates only on that window.

### 4.1 Preconditions

Before calling native read:

1. The Java buffer must be non-null.
2. The Java layer must determine `remaining = limit - position`.
3. If `remaining == 0`, Java must return `0` without entering native I/O, matching the Servlet 6.1 contract. citeturn0search0
4. For Servlet non-blocking mode, the Java layer must have established that the read is currently permitted under `isReady()` / `ReadListener` rules. Native kernel readability does not replace those Servlet rules. citeturn0search0turn0search6

### 4.2 Direct-buffer fast path

If the Java buffer is direct and JNI direct-buffer access succeeds, native code may obtain its base address and perform `recv()` directly into the remaining region.

The effective native destination is conceptually:

`address + position`

for at most `remaining` bytes.

Native code must bounds-check the requested length against the buffer capacity/window supplied by Java.

### 4.3 Heap/non-direct fallback

If the buffer is non-direct, or direct-buffer access is unavailable, native code must not call `GetDirectBufferAddress` and then dereference a `NULL` result.

The initial implementation may use a bounded temporary native scratch buffer, perform the socket read into that scratch buffer, and copy exactly the bytes read into the Java buffer through a supported JNI array/buffer operation.

The fallback is a compatibility path, not a claim about performance. It must be benchmarked separately from the direct path.

### 4.4 Result mapping

The transport result must distinguish at least:

- `bytes > 0`: successful partial/full read;
- `EOF`: peer performed an orderly read-side shutdown and no bytes were returned by that operation;
- `WOULD_BLOCK`: no data is currently available on the non-blocking socket;
- `ERROR`: an I/O error occurred.

`EINTR` must eventually be represented separately from `EAGAIN`/`EWOULDBLOCK`; the current native connection implementation temporarily maps `EINTR` to `WOULD_BLOCK`, but that mapping is not accepted as the final JNI ABI.

### 4.5 Java buffer state

The native function must not silently mutate Java `ByteBuffer.position()` or `limit()` through undocumented JNI side effects.

The Java adapter owns the semantic state transition. For the Servlet `read(ByteBuffer)` operation, after `n > 0` bytes have been read, it must preserve the original position and set the limit to `originalPosition + n`, as required by Servlet 6.1. citeturn0search0

This rule is intentionally separated from the low-level socket read result because Tomcat's internal `Http11InputBuffer` has its own position/limit state and parser invariants. Native transport must report bytes transferred; the Java/Tomcat layer decides how those bytes are incorporated into its parser buffer.

## 5. Write operation contract

The conceptual native operation is:

`writeFrom(javaByteBuffer)`

The Java adapter supplies the exact remaining source window for one native operation.

### 5.1 Partial write is normal

A non-blocking `send()` may write fewer bytes than requested. The native result therefore reports the exact number of bytes consumed from the supplied window.

The Java/Tomcat layer retains the unwritten remainder. NativeTomcat must not discard it and must not assume that `EPOLLOUT` means the entire remaining response can be written in one call.

### 5.2 EPOLLOUT ownership

`EPOLLOUT` is armed only while there is pending output that cannot currently be written without blocking.

Once pending output has been drained, write interest should be removed. This prevents an idle writable socket from generating unnecessary readiness notifications.

The event-loop owner controls the actual epoll registration/rearm operation. A Java thread must not concurrently mutate the epoll registration directly. A cross-thread request to change interest must be marshalled to the event-loop owner through the native wakeup mechanism defined by the event contract. fileciteturn206file0L2-L2

### 5.3 Servlet output semantics

The native byte count is not itself the Servlet `write(ByteBuffer)` contract. The Java adapter must enforce the Servlet 6.1 state rules, including readiness checks and the required buffer-state behavior. citeturn1search5

In particular, non-blocking output cannot be modeled as "native wrote some bytes, therefore Java may mutate the buffer arbitrarily". The adapter must preserve the Servlet-required state until the next permitted writable operation.

## 6. Readiness is layered

Native kernel readiness and Servlet readiness are different states.

### Native layer

`EPOLLIN` means the socket has a condition for which a read operation may be attempted. `EPOLLOUT` means a write operation may be attempted without blocking at that instant.

### Tomcat/Coyote layer

Tomcat maintains its own socket-wrapper, buffer, parser, timeout, and processor state. `Http11InputBuffer` may already contain data that was previously read from the socket, and therefore application-level availability cannot be reduced to a fresh `EPOLLIN` event. citeturn0search2

### Servlet layer

`ServletInputStream.isReady()` / `ServletOutputStream.isReady()` and the `ReadListener` / `WriteListener` callback rules determine when Servlet application code is permitted to perform non-blocking operations. citeturn0search0turn0search6turn1search5

Therefore:

`EPOLLIN != ServletInputStream.isReady()`

and

`EPOLLOUT != ServletOutputStream.isReady()`.

The native event loop is an implementation mechanism, not the public Servlet state machine.

## 7. EPOLLONESHOT and buffer consumption

Accepted sockets currently use `EPOLLONESHOT`. After a readiness notification, the event-loop owner must explicitly rearm the connection. fileciteturn206file0L2-L2

The future JNI adapter must therefore follow this ordering:

1. native event loop receives the readiness event;
2. native layer establishes the connection/event ownership for the work item;
3. Java/Tomcat consumes or attempts the available I/O through the bridge;
4. the resulting buffer/parser state is committed;
5. the native event-loop owner computes the next required interest set;
6. the connection is rearmed exactly once for that state.

The exact callback/queue mechanism for step 2 is deferred. It must not be implemented as an arbitrary Java-thread `epoll_ctl` call.

## 8. EOF, half-close, error, and close

A successful read returning zero bytes is EOF for the socket read operation. The native connection already records peer read closure separately from the connection's own terminal close state.

A peer read-half-close must not automatically be treated as identical to an immediate full connection destruction until the HTTP/Tomcat layer has established whether pending response output or protocol completion remains possible.

Transport errors must be propagated exactly once to the Java/Tomcat layer. Native cleanup must not result in duplicate Java callbacks for the same terminal connection event.

The final close protocol remains deferred until the JNI lifetime/Java-reference design is implemented and tested.

## 9. Lifetime and aliasing rules

The following rules are mandatory for the first bridge:

1. C never retains a pointer returned by `GetDirectBufferAddress()` after the JNI call returns.
2. C never retains a Java `ByteBuffer` reference unless a later explicit global-reference contract authorizes it.
3. Java must not retain a view onto temporary native scratch storage after the native call returns.
4. A native connection cannot be freed while an in-flight JNI operation can still access its state.
5. Buffer memory must not be recycled or overwritten while Java can legally access it.
6. The event-loop owner remains the authority for connection rearm and terminal native destruction.
7. Any cross-thread operation must use an explicit queue/wakeup mechanism before touching event-loop-owned state.

These rules prevent the first implementation from introducing use-after-free, stale-address, or double-destruction bugs at the language boundary.

## 10. TLS and protocol scope

This contract describes cleartext transport buffers only.

TLS is deliberately deferred. A TLS implementation may require encrypted network buffers, decrypted application buffers, handshake state, partial record handling, and different readiness/error semantics. The first cleartext borrowed-buffer contract must therefore not be presented as a complete TLS contract.

Likewise, HTTP/2, upgrade protocols, WebSocket framing, sendfile, compression, and zero-copy response transmission are deferred.

## 11. Required tests before freezing the JNI ABI

The implementation must not freeze the JNI method signatures until the following behaviors have executable tests:

### Buffer access

- direct buffer address acquisition succeeds on the supported JVM;
- non-direct buffer uses the fallback path;
- read-only buffer is rejected for a native write-into-buffer operation;
- zero remaining returns without socket I/O;
- capacity/position/limit bounds are enforced;
- no native pointer survives the JNI call.

### Read behavior

- partial read;
- multiple reads completing one HTTP header/body;
- `EAGAIN`/`EWOULDBLOCK`;
- `EINTR`;
- orderly EOF;
- connection reset/error;
- peer half-close with pending response data.

### Write behavior

- full write;
- partial write;
- `EAGAIN` followed by `EPOLLOUT`;
- pending-output drain and `EPOLLOUT` disarm;
- write error;
- close while output is pending.

### Servlet/Tomcat integration

- Servlet 6.1 `read(ByteBuffer)` position/limit semantics;
- Servlet 6.1 `write(ByteBuffer)` position/limit semantics;
- `isReady()` false/true transitions;
- `ReadListener.onDataAvailable()` sequencing;
- `ReadListener.onAllDataRead()`;
- `WriteListener.onWritePossible()` sequencing;
- incomplete HTTP request-line/header/body parsing across multiple native reads;
- keep-alive request sequencing;
- error and cancellation cleanup exactly once.

### Differential testing

The same HTTP workloads must be run against unmodified Tomcat 11.0.25 and NativeTomcat, with byte-for-byte comparison of relevant HTTP responses and explicit comparison of error/close behavior before any performance conclusion is drawn.

## 12. Explicit non-claims

This document does **not** claim that:

- JNI is faster than FFM;
- direct buffers are always faster than heap buffers;
- the native HTTP parser is already implemented;
- NativeTomcat is already Servlet 6.1 compatible;
- the current native event loop is production-safe for all races and resource-exhaustion conditions;
- the current buffer design is the final architecture;
- zero-copy is achieved merely because a direct buffer is used.

Those claims require implementation and measured evidence.
