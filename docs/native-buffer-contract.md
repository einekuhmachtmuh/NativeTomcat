# Native buffer contract

## 1. Scope and status

This document defines the proposed first C/Java transport-buffer contract for NativeTomcat. It is a design contract for the next JNI transport layer, not evidence that that layer is already implemented.

It deliberately separates:

- **verified upstream/spec fact** — checked against Servlet 6.1, JNI/JDK behavior, or pinned Tomcat 11.0.25 source;
- **NativeTomcat design decision** — selected architecture for the first implementation;
- **implemented** — already present in repository code;
- **deferred** — intentionally postponed;
- **blocked** — dependent verification cannot currently be completed in the local environment.

This document does not define HTTP parsing, TLS, Servlet request/response semantics, or the final JNI class/method layout.

## 2. Verified constraints

Servlet 6.1 exposes `ByteBuffer` operations on `ServletInputStream` and `ServletOutputStream`, with explicit position/limit and non-blocking readiness requirements. Those application-visible rules remain Java/Tomcat responsibilities.

Tomcat 11.0.25's `Http11InputBuffer` uses incremental `ByteBuffer` state and `SocketWrapperBase.read(...)`. A single socket read therefore cannot be treated as one complete HTTP request or header.

JNI provides direct-buffer access through `GetDirectBufferAddress()` and `GetDirectBufferCapacity()`. Direct-buffer access must be checked rather than assumed to succeed for every possible buffer/JVM combination.

These facts constrain the transport adapter but do not prescribe a particular final zero-copy architecture.

## 3. Ownership decision for the first transport path

The first transport implementation will use a **bounded borrowed Java `ByteBuffer` window** rather than introduce a persistent native request buffer.

NativeTomcat owns:

- socket FD;
- native connection state;
- epoll registration/rearm state;
- any native pending-output storage required by the transport implementation.

Tomcat/Java owns:

- `Http11InputBuffer` and its parser state;
- `SocketBufferHandler` state where used by the actual Tomcat implementation;
- Java `ByteBuffer` objects and their semantic position/limit state;
- Servlet-visible buffers.

For one native I/O operation, native code may access only the bounded memory window explicitly supplied by Java. It must not retain the pointer after the JNI call returns.

This is a design decision for the first implementation, not a claim that all Tomcat paths can permanently use borrowed buffers.

## 4. Read contract

Conceptual operation:

`readInto(javaByteBufferWindow)`

### Preconditions

1. Java supplies a non-null buffer.
2. Java determines the exact permitted window and remaining length.
3. A zero-length operation is handled without socket I/O where required by the higher-level contract.
4. Servlet non-blocking permission has already been established by Java/Tomcat. Kernel `EPOLLIN` is not a substitute for Servlet `isReady()`.

### Native operation

If the buffer is direct and JNI returns a valid address, native code may perform `recv()` into the supplied window.

If direct access is unavailable, the implementation may use a bounded operation-scoped scratch buffer and copy the exact transferred bytes through JNI. The scratch buffer must not become persistent request state.

### Result

The native transport result must distinguish at least:

- positive byte count;
- would-block;
- EOF/orderly peer shutdown;
- error.

The JNI ABI must define how interruption (`EINTR`) is represented rather than silently conflating it with would-block.

### Java buffer semantics

The native operation reports bytes transferred; the Java adapter owns the semantic update of Java buffer position/limit.

For Servlet 6.1 `read(ByteBuffer)`, the Java layer must apply the exact Servlet position/limit contract. That rule must not be delegated to undocumented native mutation.

## 5. Write contract

Conceptual operation:

`writeFrom(javaByteBufferWindow)`

A non-blocking write may consume fewer bytes than requested. The native result must report the exact number consumed.

The Java/Tomcat layer retains the unwritten remainder according to the actual `SocketWrapperBase`/write-buffer contract.

`EPOLLOUT` is armed only when pending output exists and the current transport state requires write readiness. After pending output is drained, write interest is removed from the next registration state.

A Java write completing a native call does not prove that the peer has received all response bytes.

## 6. Buffer lifetime and aliasing

The first implementation must obey:

1. no native pointer returned by `GetDirectBufferAddress()` survives the JNI call;
2. no Java buffer reference is retained by native code unless an explicit later global-reference contract authorizes it;
3. temporary native scratch storage is never exposed as a Java buffer beyond the operation that owns it;
4. a native connection cannot be destroyed while an in-flight JNI operation accesses it;
5. native output storage cannot be recycled while Java/Tomcat still owns a pending write operation;
6. Java cannot assume a native pointer remains stable across calls;
7. event-loop-owned registration and destruction remain under the native runtime owner.

These are lifetime requirements, not performance optimizations.

## 7. EPOLLONESHOT ordering

The transport buffer contract depends on the event contract's one-shot ownership rule:

```text
native readiness
    -> establish event/work ownership
    -> perform/attempt transport consumption
    -> commit Java/Tomcat buffer/parser state
    -> determine next read/write interest
    -> native owner rearms exactly once
```

The current repository has not yet completed this hand-off: JNI event dispatch currently queues Java work without performing transport consumption. Therefore the above is the **required target ordering**, not a claim about current runtime behavior.

A Java worker must not call `epoll_ctl` directly to bypass this ownership model.

## 8. EOF, half-close and error

Peer read-half-close, orderly EOF, transport error and local close are distinct transport observations.

A peer half-close must not automatically destroy a connection before the protocol layer has determined whether pending response output or other protocol completion remains possible.

The native layer must avoid duplicate terminal delivery when multiple epoll flags describe one terminal condition. Exactly-once propagation will be finalized together with Java wrapper lifetime.

## 9. Readiness layers

The project must preserve:

```text
kernel readiness
    != native event ownership
    != Tomcat transport state
    != Servlet isReady()
```

Tomcat may already have parser/input data available without a new kernel readiness event. Conversely, kernel readability does not make arbitrary Servlet non-blocking reads legal.

## 10. Cleartext-only scope

This contract is for the initial cleartext transport path.

TLS may introduce encrypted/decrypted buffers, handshake state, record boundaries and different readiness/error interactions. HTTP/2, upgrade protocols, WebSocket framing, compression, sendfile and zero-copy response transmission are also outside this first contract.

## 11. ABI-freeze test gate

Do not freeze the JNI transport ABI until executable tests cover:

### Native transport

- partial read/write;
- would-block;
- `EINTR`;
- EOF and half-close;
- reset/error;
- pending output and `EPOLLOUT` arm/disarm;
- exactly-once rearm;
- close while output is pending.

### Buffer boundary

- direct-buffer access;
- non-direct fallback if supported;
- read-only rejection for write-into-buffer operations;
- zero remaining;
- position/limit/capacity bounds;
- no pointer retained after JNI return.

### Tomcat/Servlet integration

- `SocketWrapperBase` read/write semantics;
- incremental HTTP parsing across multiple native reads;
- Servlet 6.1 `ByteBuffer` position/limit behavior;
- `isReady()` transitions;
- `ReadListener` / `WriteListener` sequencing;
- keep-alive sequencing;
- exactly-once close/error cleanup.

### Differential test

The same representative HTTP workloads must be run against unmodified pinned Tomcat 11.0.25 and NativeTomcat. Response bytes, status/error behavior and relevant close behavior must be compared before performance conclusions are made.

## 12. Explicit non-claims

This document does not claim:

- the JNI transport bridge is already implemented;
- direct buffers are always faster;
- JNI is faster than FFM;
- native HTTP parsing is implemented;
- NativeTomcat is already Servlet 6.1 compatible;
- direct buffers automatically provide zero-copy;
- the current event-loop lifetime protocol is race-free under full Java integration.
