# JNI Request/Response Bridge Options

Status: design record + local JDK 21 HotSpot microbenchmark evidence. No request/response bridge is implemented by this document.

## Purpose

This document records the JDK 21 HotSpot-based cost model used to choose a NativeTomcat C↔Java request/response bridge. The model must be validated against the implemented Java contract and benchmarked on the target JVM/CPU before any production decision.

The repository has native socket/epoll/connection and JVM bootstrap pieces, but no completed request/response JNI bridge. The three benchmark drivers under `native/test/JNI_bridge_test/` are isolated microbenchmark fixtures; they are not the NativeTomcat production ABI and do not establish Servlet/Tomcat integration equivalence.

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

- Per-request Java objects are created by the bridge for the request envelope (example model: `Request`, request strings, request `byte[]`) and the Java target creates the `Response` used by this benchmark.
- Field access is `O(F)` in the number of marshalled fields.
- Payload/string conversion is linear in the number of bytes copied/converted.
- Example contract used in the cost model: request `method/path/query/body`, response `status/contentType/body`.
- The concrete benchmark contract below uses 16 hot-path JNI API calls/request, excluding cleanup calls. This is a contract-specific accounting number, not a universal JNI fact.

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
- Cost model: `T_C = C_JNI + C_dispatch + T_Java`, i.e. `Θ(1)` with respect to payload size `N/M` for the bridge boundary itself.

## Benchmark fixture and method

The reproducible fixture is under `native/test/JNI_bridge_test/`:

- `field_marshal.c`: field-by-field request construction and response extraction.
- `region.c`: reusable `byte[]` plus `SetByteArrayRegion`/`GetByteArrayRegion`.
- `direct.c`: retained `DirectByteBuffer`, with both a minimal no-copy target and a Java-side byte-copy target.
- `BridgeTarget.java`: the deliberately small Java target contract used by all three drivers.
- `common.h`: JNI VM startup, monotonic timing, exception checking and percentile reporting.

The drivers were compiled locally with OpenJDK 21.0.11 (`/usr/lib/jvm/java-21-openjdk-amd64`) and GCC using `-O3 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2 -fPIC`, linking against the JDK 21 `libjvm`, `pthread` and `dl`.

Each benchmark performs 10,000 warm-up iterations followed by 100,000 measured iterations. Timing uses `clock_gettime(CLOCK_MONOTONIC_RAW)`. Payload sizes tested were 64, 1,024 and 16,384 bytes. Each driver also performs a payload correctness check before the measured loop; all three checks passed.

These are microbenchmark measurements on one local environment, not end-to-end NativeTomcat measurements. The measured "full bridge" intervals include the JNI calls, the benchmark Java target, data copies performed by that target, and timing overhead. They therefore must not be reinterpreted as a pure per-JNI-call cost. The separately measured steps are useful for identifying where this particular fixture spends time, but their sums should not be treated as exact independent additive costs because each timing interval contains measurement overhead and the JVM may optimize differently across contexts.

## Measured results

### Summary at 64, 1,024 and 16,384 bytes

The following values are the final verification run; values are `p50 / p95 / p99 / avg`, in ns/request.

| Payload | Field-marshal full bridge | `byte[]` / Region full bridge | DirectByteBuffer no-copy | DirectByteBuffer + Java-side copy |
|---:|---:|---:|---:|---:|
| 64 B | 521 / 1532 / 1823 / 632 | 151 / 180 / 220 / 175 | 90 / 101 / 140 / 92 | 100 / 120 / 140 / 110 |
| 1,024 B | 551 / 1592 / 1832 / 719 | 180 / 200 / 250 / 193 | 90 / 101 / 130 / 93 | 291 / 310 / 340 / 305 |
| 16,384 B | 1833 / 5068 / 6240 / 2601 | 1051 / 1082 / 1311 / 1482 | 90 / 120 / 160 / 111 | 3195 / 3215 / 4166 / 3396 |

The result supports the qualitative boundary-cost ordering in this fixture: field-marshal has the largest fixed metadata/object work; Region has fewer JNI crossings but copies the payload; DirectByteBuffer has one hot-path JNI call and no JNI-side payload copy. The result does **not** prove that DirectByteBuffer will be faster end-to-end in NativeTomcat, because the real Tomcat parser, buffering, ownership, backpressure and response path are not present in this fixture.

### Field-marshal: per-step measurement

At `N = 1,024` bytes, the final run measured:

| Step | min | p50 | p95 | p99 | avg |
|---|---:|---:|---:|---:|---:|
| Full bridge/request | 510 | 551 | 1,592 | 1,832 | 719 |
| `NewObject(Request)` | 90 | 100 | 120 | 150 | 112 |
| `NewStringUTF ×3` | 140 | 151 | 171 | 511 | 172 |
| `NewByteArray + SetByteArrayRegion` | 60 | 70 | 1,042 | 1,262 | 189 |
| `SetObjectField ×4` | 40 | 50 | 51 | 60 | 48 |
| Java dispatch target | 80 | 100 | 140 | 160 | 116 |
| Response field extraction | 60 | 70 | 90 | 100 | 79 |

The concrete hot path is exactly 16 JNI API calls when cleanup calls are excluded:

```text
NewObject(Request)             1
NewStringUTF                   3
NewByteArray                   1
SetByteArrayRegion             1
SetObjectField                 4
CallStaticObjectMethod         1
GetIntField                    1
GetObjectField                 2
GetStringUTFRegion              1
GetByteArrayRegion              1
---------------------------------
TOTAL                         16
```

The benchmark's concrete per-request Java object creation is **6 objects**, not 4: one `Request`, three request `String` objects, one request `byte[]`, and one `Response`. The `contentType` value is the Java string literal `"text/plain"` in `BridgeTarget.java`, so it is not a new string allocation per measured invocation in this fixture. The earlier comparison-table value of 4 was therefore inconsistent with the concrete contract and has been corrected to 6 for this benchmark. The count remains contract-specific and should not be generalized to a real Tomcat request object graph.

The payload-size sweep also shows the expected growth of the field path. Full-bridge p50 was about 521 ns at 64 B, 551 ns at 1,024 B, and 1,833 ns at 16,384 B. In the same runs, the `NewByteArray + SetByteArrayRegion` p50 rose from 50 ns to 70 ns to 1,042 ns, while response extraction rose from 60 ns to 70 ns to 311 ns. This is evidence that payload transfer becomes a substantial part of this field-marshal fixture as the body grows.

### `byte[]` + Region: per-step measurement

At `N = 1,024` bytes:

| Step | min | p50 | p95 | p99 | avg |
|---|---:|---:|---:|---:|---:|
| Full bridge/request | 170 | 180 | 200 | 250 | 193 |
| `SetByteArrayRegion(N)` | 30 | 40 | 41 | 50 | 38 |
| Java dispatch target | 90 | 100 | 120 | 141 | 107 |
| `GetByteArrayRegion(M)` | 40 | 40 | 50 | 51 | 47 |

At 64 B, full-bridge p50 was 151 ns; at 16,384 B it was 1,051 ns. The measured Region copy steps likewise increased: input Region p50 30 → 40 → 321 ns and output Region p50 31 → 40 → 330 ns across 64 → 1,024 → 16,384 B. This directly supports the `N + M` copy model for this fixture.

The Java arrays are allocated once before the measured loop and reused, so the benchmark does not introduce a per-request Java array allocation on the Region path. The correctness check compared the native input and output buffers after warm-up and passed.

### DirectByteBuffer: setup and hot path

At `N = 1,024` bytes, setup was measured separately because it is not part of the per-request hot path:

```text
NewDirectByteBuffer ×2:     119,208 ns
GetDirectBufferAddress ×2:      260 ns
```

The exact one-time setup duration varied between runs and payload sizes, so it must not be amortized into a universal constant. The important semantic check is that the two addresses returned by `GetDirectBufferAddress` exactly matched the native input/output allocations used to construct the direct buffers.

For the hot path at 1,024 B:

| Target | min | p50 | p95 | p99 | avg |
|---|---:|---:|---:|---:|---:|
| JNI call + minimal no-copy Java target | 80 | 90 | 101 | 130 | 93 |
| JNI call + Java-side 1,024-B copy | 280 | 291 | 310 | 340 | 305 |

The no-copy p50 remained approximately flat across 64, 1,024 and 16,384 B (90, 90 and 90 ns respectively). By contrast, the Java-side copy p50 increased from 100 ns at 64 B to 291 ns at 1,024 B and 3,195 ns at 16,384 B. This is the critical guardrail: **zero JNI bridge copy is not equivalent to zero data-movement cost for the complete Java/Tomcat path**. If Java actually reads/writes the direct buffer, those accesses still have a payload-dependent cost.

## What the measurements confirm and what they do not

### Confirmed by the fixture

1. The three concrete benchmark call shapes are executable on OpenJDK 21.0.11.
2. The field-marshal fixture executes the documented 16 hot-path JNI API calls.
3. The concrete field-marshal benchmark creates six Java objects per measured request under its deliberately small target contract; the previous table value of four was incorrect.
4. The Region fixture uses three hot-path JNI API calls and reusable arrays.
5. The Region measurements increase with payload size in the expected direction for the input/output copies.
6. The DirectByteBuffer fixture creates the direct buffers during setup, verifies the native addresses, and uses one JNI dispatch call per measured request.
7. The DirectByteBuffer no-copy target remains approximately constant with payload size, while the Java-side buffer-copy target grows strongly with payload size.
8. All three fixtures passed their payload correctness checks and completed 100,000 measured iterations at each tested size.

### Not confirmed by the fixture

1. Absolute JNI costs on other CPUs, OSes, JDK builds or JVM configurations.
2. NativeTomcat end-to-end request latency or throughput.
3. Tomcat `SocketWrapperBase`, `AbstractEndpoint`, protocol parser, Coyote, Catalina or Servlet execution costs.
4. GC impact under a real request mix.
5. Native buffer lifetime, backpressure, cancellation, request-body consumption, response production or thread-affinity correctness in the real NativeTomcat architecture.
6. Whether DirectByteBuffer is faster once real Tomcat processing is included.
7. End-to-end zero-copy: the benchmark only establishes that the JNI bridge itself does not copy the payload when direct buffers are reused.

## Cost comparison

Let:

- `N` = request payload bytes crossing the boundary.
- `M` = response payload bytes crossing the boundary.
- `F` = number of request fields marshalled individually.
- `L` = bytes in strings converted/copied by the bridge.
- `T_Java` = Java/Tomcat/application processing, deliberately excluded from the bridge-only theoretical comparison.

| Bridge | Hot-path JNI API calls | Bridge Java allocations/request | Payload copy/request | Asymptotic bridge model |
|---|---:|---:|---:|---|
| Field-marshal | contract-dependent; measured example = 16 | 6 in the measured example | `N + M` plus string conversion | `Θ(F + L + N + M) + T_Java` |
| `byte[]` / Region | 3 | 0 | `N + M` | `3*C_JNI + Θ(N + M) + T_Java` |
| DirectByteBuffer | 1 | 0 | 0 at the JNI bridge | `C_JNI + C_dispatch + T_Java` |

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

The benchmark is consistent with this ordering, but only for the isolated fixture. It is not evidence that a production implementation should immediately choose DirectByteBuffer.

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

The microbenchmark confirms why the distinction matters: DirectByteBuffer's JNI-side payload copy remains 0 bytes, but the Java-side copy target grows from about 100 ns p50 at 64 B to 3,195 ns p50 at 16,384 B.

Actual nanoseconds, CPU instruction counts, cache misses, branch behavior, and GC pause effects cannot be derived honestly from JDK source alone. They require a controlled benchmark on the target JDK/CPU.

## NativeTomcat implementation direction

Do not implement all three bridges in production merely because they are documented here. Use this record to guide a staged decision:

1. Define the actual Java request/response contract against the pinned Tomcat source version vendored or incrementally mirrored by this repository.
2. Preserve existing native connection/buffer ownership rules.
3. Prototype the smallest semantically correct bridge first.
4. Prefer reusable `byte[]`/Region as the correctness baseline because its ownership contract is straightforward and the benchmark demonstrates a small, explicit three-call boundary.
5. Evaluate DirectByteBuffer as the performance-oriented target only after proving buffer lifetime, backpressure, request-body consumption, response production, exception propagation, thread affinity, and Servlet/Tomcat semantics.
6. Treat field-marshal as a compatibility/reference design, not as the default high-throughput path, unless the final contract shows that the metadata volume is small enough to justify it.
7. Add microbenchmarks that separately measure JNI-call overhead, allocation, copies, Java target execution, and end-to-end request latency. The fixture in `native/test/JNI_bridge_test/` is the baseline for the first three bridge shapes; it must not be mistaken for the final end-to-end benchmark.

## Non-goals / guardrails

- This document does not authorize changing the current C/Java ABI.
- It does not claim that NativeTomcat already implements any of the three bridges.
- It does not claim that DirectByteBuffer makes the entire HTTP/Tomcat pipeline zero-copy.
- It does not prescribe a Java object layout before the real Tomcat integration point is verified.
- Any future implementation must re-check the exact JDK 21 source path and the exact pinned Tomcat source path before relying on a cost count.
- The benchmark numbers in this document are measured results from one environment, not universal constants.
