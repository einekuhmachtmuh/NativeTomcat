# C/Java Boundary Design

## 1. Verified constraints

NativeTomcat targets Jakarta Servlet 6.1 semantics. Servlet 6.1 adds `ByteBuffer` support to `ServletInputStream` and `ServletOutputStream`. The input-stream contract requires the container to preserve the buffer position on return and advance the limit by the number of bytes read; the output-stream contract requires the position to reach the limit when all remaining data has been written. Non-blocking input/output also has explicit `isReady()` / listener sequencing rules. These are Servlet semantics and must remain observable regardless of the native implementation. citeturn0search0turn0search7turn0search24

JNI provides direct-buffer operations (`NewDirectByteBuffer`, `GetDirectBufferAddress`, `GetDirectBufferCapacity`) and has supported them since JDK/JRE 1.4. A direct buffer can expose native memory to Java without requiring a Java heap byte-array copy. This makes JNI direct buffers a viable first bridge for a JDK 17+ compatibility path. citeturn1search0turn1search6

Tomcat 11.0.25 itself uses JNI for Tomcat Native loading and declares synchronized native-library initialization/termination semantics. This is evidence that JNI is an established interop mechanism in the target Tomcat version, although NativeTomcat will not reuse Tomcat Native's APR ownership model. fileciteturn69file0L2-L2

Tomcat 11.0.25 also contains a separate FFM/Panama OpenSSL implementation using `java.lang.foreign.Arena`, `MemorySegment` and related APIs. Therefore FFM cannot be treated as universally available on the project's relaxed JDK floor. It remains an optional later bridge. fileciteturn67file0L2-L2

## 2. First bridge choice

The first NativeTomcat C↔Java bridge will use **JNI + direct `ByteBuffer`**, not FFM and not Java heap arrays as the normal data path.

Reasons:

1. JNI is available on the JDK 17 compatibility floor.
2. JNI direct-buffer APIs are standardized and long-standing.
3. Direct buffers can represent native memory without an obligatory heap-array copy.
4. Servlet 6.1 explicitly exposes `ByteBuffer` operations, so the bridge can map naturally onto the API without inventing a new application-visible data model.
5. FFM can be evaluated independently after the minimum JDK required by the exact Tomcat/FFM source and build path has been measured.

This is an initial engineering decision, not a claim that JNI has lower overhead than FFM. A later benchmark must measure call overhead, buffer transfer cost, allocation, GC/direct-memory pressure and end-to-end request latency.

## 3. Ownership model

### NativeConnection

The native side owns the socket descriptor and the native connection state while the connection is active in the native event loop.

Java must never close the descriptor directly. Java requests native close/rearm operations through the bridge. The native connection is destroyed only after all outstanding Java-visible references/operations have reached a terminal state.

### Native input buffer

The preferred request-input representation is native-owned storage exposed to Java as a direct `ByteBuffer` for the duration of an explicitly bounded lease.

Rules:

- Java receives a view, not ownership of the underlying allocation.
- The native allocation must remain alive until the lease ends.
- Java must not retain the buffer after the lease expires.
- Native code must not recycle or overwrite the region while Java can still access it.
- A buffer may be copied into Java-owned storage only when required by Servlet/Tomcat semantics or when the application explicitly causes retention beyond the native lease.

### Native output buffer

The native side owns the write queue and its storage. Java receives a direct `ByteBuffer` view only for a bounded write operation. If Servlet semantics require the bytes to remain pending after the Java call returns, the native side retains them in its own write queue and controls the lifetime explicitly.

## 4. Request/response representation

The first bridge must not expose the complete Tomcat internal `Request` or `Response` object graph to C. Those objects carry Servlet/Catalina semantics and implementation state that should remain in Java.

The native side should expose only transport-level state initially:

| Data | Initial owner | Java view | Lifetime |
|---|---|---|---|
| socket fd | C | none | connection |
| readiness state | C | derived through callbacks/state methods | connection |
| request byte storage | C | direct `ByteBuffer` lease | read lease |
| response write storage | C | direct `ByteBuffer` lease | write lease |
| HTTP parse state | C only after differential parser is validated | no direct object exposure initially | request |
| Servlet Request/Response objects | Java | normal Servlet API | Servlet lifecycle |
| FilterChain/Servlet lifecycle | Java | normal Servlet API | dispatch |

The boundary therefore separates **transport ownership** from **Servlet semantics** rather than mirroring every Tomcat class in C.

## 5. Thread affinity

A native connection has an owning event-loop context. Native connection mutation is restricted to that context unless an operation is explicitly marshalled to it.

JNI calls made from native event-loop threads must first establish a valid `JNIEnv*` for that thread. Threads created by NativeTomcat that call into Java must be attached to the JVM and detached before thread termination. The bridge must never cache a `JNIEnv*` globally because `JNIEnv*` is thread-specific.

Java object references retained by native code must be explicit JNI global references and must be released before the owning native object is destroyed. Local JNI references must not escape their JNI call scope.

## 6. Error and cancellation states

The bridge will use explicit terminal states rather than relying on null pointers or implicit cleanup:

`ACTIVE → CLOSING → CLOSED`

and, for a request operation:

`OPEN → DISPATCHING → COMPLETED | FAILED | CANCELLED`

A cancellation/error transition must make subsequent buffer access invalid and deterministic. Native cleanup must be idempotent at the state-machine level, while the public API must still define which caller owns the final destruction operation.

## 7. Servlet non-blocking semantics

Native readiness must not be exposed as a replacement for Servlet `isReady()` semantics. The Java `ServletInputStream`/`ServletOutputStream` implementation remains responsible for enforcing the Servlet 6.1 call-order rules and listener callbacks. Native readiness is an implementation mechanism used to decide when Java may be notified.

In particular, an epoll-readable event does not by itself mean that arbitrary Java `read()` calls are legal. The Java layer must maintain the Servlet state required by `ReadListener`, `WriteListener`, async processing and completion.

## 8. Crossing policy

The bridge should prefer bulk operations over per-byte or per-header JNI calls.

Initial policy:

- one native readiness event may produce one Java callback/work item;
- request metadata should cross in bulk rather than through repeated JNI field access;
- body data should use direct buffers where lifetime permits;
- response data should be queued in native storage rather than copied repeatedly through byte arrays;
- strings should not cross the boundary repeatedly for low-level transport operations;
- JNI exception state must be checked after calls that can throw.

No performance claim is attached to these choices until benchmark data exists.

## 9. Deliberately deferred

The following are not yet implemented or fixed by this document:

- exact Java package/class names;
- JNI method registration layout;
- HTTP parser ownership;
- Tomcat `CoyoteAdapter` integration;
- Servlet request/response subclass implementation;
- async dispatch integration;
- HTTP/2;
- TLS;
- FFM replacement/alternative;
- cross-request buffer pooling;
- zero-copy response transmission;
- final native connection destruction protocol.

These require source-level integration against the Tomcat 11.0.25 tree and differential tests before implementation is frozen.
