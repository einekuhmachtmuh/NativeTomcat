# JNI Request/Response Bridge Options

Status: design record only. No request/response bridge is implemented by this document.

## Purpose

This document records the JDK 21 HotSpot-based cost model used to choose a NativeTomcat C↔Java request/response bridge. Future implementation work must treat these numbers as a source-grounded model, then validate the exact call sequence against the implemented Java contract and benchmark it on the target JVM/CPU.

The current repository has the native socket/epoll/connection and JVM bootstrap pieces, but no completed request/response JNI bridge. Do not interpret the examples below as existing APIs.

## Three bridge shapes

### 1. Field-marshal

```text
native HTTP state
    │
    ├─ NewObject(Request)
    ├─ NewString*(path/query/...)
    ├─ NewByteArray(body)
    ├─ Set*Field × F
    └─ CallObjectMethod(dispatch)
              │
              ▼
       Java Request object
              │
              ▼
       Java application/Tomcat
              │
              ▼
       Java Response object
    ├─ Get*Field × F'
    ├─ GetString*Region
    └─ GetByteArrayRegion
              │
              ▼
        native response
```

Characteristics:

- Per-request Java objects are created by the bridge for the request envelope (example model: `Request`, request strings, request `byte[]`).
- Field access is `O(F)` in the number of marshalled fields.
- Payload/string conversion is linear in the number of bytes copied/converted.
- Example contract used in the cost model: request `method/path/query/body`, response `status/contentType/body`.
- The earlier illustrative count was 16 JNI API calls/request for this concrete contract. This is a contract-specific accounting number, not a universal JNI fact; implementation must recount from the final source.

### 2. `byte[]` + Region

```text
native input buffer
       │
       │ SetByteArrayRegion(N)
       ▼
   reusable byte[]
       │
       │ CallIntMethod(...)
       ▼
 Java/Tomcat parser
       │
       ▼
   reusable byte[]
       │
       │ GetByteArrayRegion(M)
       ▼
native output buffer
```

Characteristics:

- Input/output arrays are created once and reused; they must not be allocated per request.
- Hot-path model: 3 JNI API calls/request: input Region, Java dispatch, output Region.
- New Java heap allocations by the bridge per request: 0, assuming reusable arrays and no bridge-created wrapper objects.
- New user-visible local JNI references per request: 0 if the reusable arrays and method ID are retained appropriately.
- Payload copy: `N + M` bytes/request.
- Cost model: `T_B = 3*C_JNI + Θ(N + M) + T_Java`.

### 3. DirectByteBuffer / zero-copy bridge

```text
          native input memory
                 │
                 │ same address
                 ▼
          DirectByteBuffer
                 │
                 │ CallIntMethod(...)
                 ▼
          Java/Tomcat
                 │
                 ▼
         DirectByteBuffer
                 │
                 │ same native output memory
                 ▼
          native output memory
                 │
                 ▼
                send()
```

Characteristics:

- Native input/output memory is owned by the native connection/buffer layer.
- A `DirectByteBuffer` is created during initialization and retained; it is not recreated per request.
- `GetDirectBufferAddress` should be used during setup if needed and the resulting native address cached; it should not be unnecessarily repeated in the hot path.
- Hot-path model: 1 JNI API call/request for the Java dispatch when the buffers and method ID are cached.
- New Java heap allocations by the bridge per request: 0.
- New user-visible local JNI references per request: 0, assuming cached global references.
- Payload copy attributable to the JNI bridge: 0 bytes.
- Cost model: `T_C = C_JNI + C_dispatch + T_Java`, i.e. `Θ(1)` with respect to payload size `N/M`.

## Cost comparison

Let:

- `N` = request payload bytes crossing the boundary.
- `M` = response payload bytes crossing the boundary.
- `F` = number of request fields marshalled individually.
- `L` = bytes in strings converted/copied by the bridge.
- `T_Java` = Java/Tomcat/application processing, deliberately excluded from the bridge-only comparison.

| Bridge | Hot-path JNI API calls | Bridge Java allocations/request | Payload copy/request | Asymptotic bridge model |
|---|---:|---:|---:|---|
| Field-marshal | contract-dependent; illustrative model = 16 | 4 in the illustrative request envelope | `N + M` plus string conversion | `Θ(F + L + N + M) + T_Java` |
| `byte[]` / Region | 3 | 0 | `N + M` | `3*C_JNI + Θ(N + M) + T_Java` |
| DirectByteBuffer | 1 | 0 | 0 | `C_JNI + C_dispatch + T_Java` |

The ordering expected from the boundary-only model is:

```text
field-marshal  ──────────────── most boundary work
      │
      ▼
byte[] / Region ────────────── fewer calls, still copies payload
      │
      ▼
DirectByteBuffer ───────────── no JNI payload copy; fixed call overhead
```

## JDK 21 HotSpot source facts that constrain implementation

The model is based on JDK 21 HotSpot JNI implementation, not JNI documentation or third-party summaries. The important source-level facts are:

1. `NewObject` reaches HotSpot instance allocation and creates a JNI local handle for the returned object.
2. `Set*Field` resolves the JNI handles and performs a HotSpot field store; reference stores use the JVM's normal reference-store/barrier machinery rather than being a raw C assignment.
3. `Set<Type>ArrayRegion` performs a native-to-Java array copy; `Get<Type>ArrayRegion` performs the reverse copy. Therefore their payload cost is linear in the region length.
4. `Call<Type>Method*` constructs JNI call arguments and enters the HotSpot Java-call machinery. One JNI API call is therefore not one machine instruction and must not be treated as such in performance estimates.
5. JNI local/global references are handles. A zero-allocation hot path still has JNI handle machinery around Java calls; do not equate "0 new local references" with "0 handle operations internally".
6. `NewDirectByteBuffer` creates a Java `DirectByteBuffer` object around an existing native address/capacity; it does not copy the represented payload into a Java heap array.
7. `GetDirectBufferAddress` obtains the native address represented by a direct buffer. Cache the address where the ownership/lifetime contract permits it.

## JIT boundary

JIT compilation changes the cost of the Java target (`T_Java`) and may reduce Java-side work after the call enters compiled code. It does **not** turn `SetByteArrayRegion`/`GetByteArrayRegion` into zero-copy operations, and it does not fuse an arbitrary sequence of native JNI field calls into one native struct transfer.

Therefore:

```text
JIT can reduce:       Java target / Java-side computation
JIT cannot remove:    native↔Java Region copies already required by JNI
JIT cannot assume:    a whole JNI call sequence becomes one instruction
```

Actual nanoseconds, CPU instruction counts, cache misses, branch behavior, and GC pause effects cannot be derived honestly from JDK source alone. They require a controlled benchmark on the target JDK/CPU.

## NativeTomcat implementation direction

Do not implement all three bridges in production merely because they are documented here. Use this record to guide a staged decision:

1. Define the actual Java request/response contract against the Tomcat source version vendored by this repository.
2. Preserve existing native connection/buffer ownership rules.
3. Prototype the smallest semantically correct bridge first.
4. Prefer reusable `byte[]`/Region as the correctness baseline because its ownership contract is straightforward.
5. Evaluate DirectByteBuffer as the performance-oriented target only after proving buffer lifetime, backpressure, request-body consumption, response production, exception propagation, thread affinity, and Servlet/Tomcat semantics.
6. Treat field-marshal as a compatibility/reference design, not as the default high-throughput path, unless the final contract shows that the metadata volume is small enough to justify it.
7. Add microbenchmarks that separately measure JNI-call overhead, allocation, copies, Java target execution, and end-to-end request latency. Do not infer absolute ns values from this document.

## Non-goals / guardrails

- This document does not authorize changing the current C/Java ABI.
- It does not claim that NativeTomcat already implements any of the three bridges.
- It does not claim that DirectByteBuffer makes the entire HTTP/Tomcat pipeline zero-copy.
- It does not prescribe a Java object layout before the real Tomcat integration point is verified.
- Any future implementation must re-check the exact JDK 21 source path and the exact Tomcat source path before relying on a cost count.
