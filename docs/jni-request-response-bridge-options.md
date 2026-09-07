# JNI Request/Response Bridge Options

Status: design record + local JDK 21 HotSpot microbenchmark evidence. No request/response bridge is implemented by this document.

## Purpose

This document records the JDK 21 HotSpot-based cost model used to choose a NativeTomcat C↔Java request/response bridge. The model must be validated against the implemented Java contract and benchmarked on the target JVM/CPU before any production decision.

The repository has native socket/epoll/connection and JVM bootstrap pieces, but no completed request/response JNI bridge. The benchmark drivers under `native/tests/JNI_bridge_test/` are isolated microbenchmark fixtures; they are not the NativeTomcat production ABI and do not establish Servlet/Tomcat integration equivalence.

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
- The concrete benchmark contract uses 16 hot-path JNI API calls/request, excluding cleanup calls. This is a contract-specific accounting number, not a universal JNI fact.

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
- Payload copy attributable to the JNI bridge: 0 bytes.
- Cost model: `T_C = C_JNI + C_dispatch + T_Java`, i.e. `Θ(1)` with respect to payload size `N/M` for the bridge boundary itself.

## Benchmark fixture and method

The reproducible fixture is under `native/tests/JNI_bridge_test/`:

- `field_marshal.c`: field-by-field request construction and response extraction.
- `region.c`: reusable `byte[]` plus `SetByteArrayRegion`/`GetByteArrayRegion`.
- `direct.c`: retained `DirectByteBuffer`, with both a minimal no-copy target and a Java-side byte-copy target.
- `BridgeTarget.java`: the deliberately small Java target contract used by all three drivers.
- `common.h`: JNI VM startup, monotonic timing, exception checking and percentile reporting.

The drivers were compiled locally with OpenJDK 21.0.11 (`/usr/lib/jvm/java-21-openjdk-amd64`) and GCC using `-O3 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2 -fPIC`, linking against the JDK 21 `libjvm`, `pthread` and `dl`.

Each benchmark performs 10,000 warm-up iterations followed by 100,000 measured iterations. Timing uses `clock_gettime(CLOCK_MONOTONIC_RAW)`. Payload sizes tested were 64, 1,024 and 16,384 bytes. Each driver performs a payload correctness check before the measured loop; all three checks passed.

These are microbenchmark measurements on one local environment, not end-to-end NativeTomcat measurements. The measured full-bridge intervals include the JNI calls, the benchmark Java target, data copies performed by that target, and timing overhead. They must not be reinterpreted as pure per-JNI-call costs. Separately measured steps identify where this fixture spends time, but their sums must not be treated as exact independent additive costs.

## Measured results

The following are the final verification measurements. Values are `p50 / p95 / p99 / avg`, in ns/request.

| Payload | Field-marshal full bridge | `byte[]` / Region full bridge | DirectByteBuffer no-copy | DirectByteBuffer + Java-side copy |
|---:|---:|---:|---:|---:|
| 64 B | 521 / 1532 / 1823 / 632 | 151 / 180 / 220 / 175 | 90 / 101 / 140 / 92 | 100 / 120 / 140 / 110 |
| 1,024 B | 551 / 1592 / 1832 / 719 | 180 / 200 / 250 / 193 | 90 / 101 / 130 / 93 | 291 / 310 / 340 / 305 |
| 16,384 B | 1833 / 5068 / 6240 / 2601 | 1051 / 1082 / 1311 / 1482 | 90 / 120 / 160 / 111 | 3195 / 3215 / 4166 / 3396 |

The result supports the qualitative boundary-cost ordering in this fixture: field-marshal has the largest fixed metadata/object work; Region has fewer JNI crossings but copies the payload; DirectByteBuffer has one hot-path JNI call and no JNI-side payload copy. This does not prove that DirectByteBuffer will be faster end-to-end in NativeTomcat because the real Tomcat parser, buffering, ownership, backpressure and response path are not present in this fixture.

### Field-marshal: per-step measurement

At `N = 1,024` bytes:

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
GetStringUTFRegion             1
GetByteArrayRegion             1
---------------------------------
TOTAL                         16
```

The concrete per-request Java object creation in this benchmark is **6 objects**, not 4: one `Request`, three request `String` objects, one request `byte[]`, and one `Response`. The `contentType` value is the Java string literal `"text/plain"`, so it is not a new string allocation per measured invocation in this fixture. The former value of 4 was inconsistent with the concrete contract and is corrected to 6. This remains a benchmark-specific count and must not be generalized to a real Tomcat request object graph.

Across 64 → 1,024 → 16,384 B, full-bridge p50 was 521 → 551 → 1,833 ns. `NewByteArray + SetByteArrayRegion` p50 was approximately 50 → 70 → 1,042 ns, and response extraction increased to approximately 60 → 70 → 311 ns. Payload transfer therefore becomes a substantial part of this field-marshal fixture as the body grows.

### `byte[]` + Region: per-step measurement

At `N = 1,024` bytes:

| Step | min | p50 | p95 | p99 | avg |
|---|---:|---:|---:|---:|---:|
| Full bridge/request | 170 | 180 | 200 | 250 | 193 |
| `SetByteArrayRegion(N)` | 30 | 40 | 41 | 50 | 38 |
| Java dispatch target | 90 | 100 | 120 | 141 | 107 |
| `GetByteArrayRegion(M)` | 40 | 40 | 50 | 51 | 47 |

At 64 B, full-bridge p50 was 151 ns; at 16,384 B it was 1,051 ns. Input Region p50 increased approximately 30 → 40 → 321 ns and output Region p50 approximately 31 → 40 → 330 ns across 64 → 1,024 → 16,384 B. This directly supports the `N + M` copy model for this fixture.

The Java arrays are allocated once before the measured loop and reused, so the Region benchmark does not introduce a per-request Java array allocation.

### DirectByteBuffer: setup and hot path

At `N = 1,024` bytes, setup was measured separately from the per-request hot path:

```text
NewDirectByteBuffer ×2:     119,208 ns
GetDirectBufferAddress ×2:      260 ns
```

The exact one-time setup duration varied between runs and payload sizes and must not be treated as a universal constant. The semantic check verified that both addresses returned by `GetDirectBufferAddress` matched the native input/output allocations used to construct the direct buffers.

For the hot path at 1,024 B:

| Target | min | p50 | p95 | p99 | avg |
|---|---:|---:|---:|---:|---:|
| JNI call + minimal no-copy Java target | 80 | 90 | 101 | 130 | 93 |
| JNI call + Java-side 1,024-B copy | 280 | 291 | 310 | 340 | 305 |

The no-copy p50 remained approximately flat at 90 ns across 64, 1,024 and 16,384 B. The Java-side copy p50 increased from 100 ns to 291 ns to 3,195 ns. Therefore **zero JNI bridge copy is not equivalent to zero data-movement cost for the complete Java/Tomcat path**.

## What the measurements confirm

1. The three benchmark call shapes are executable on OpenJDK 21.0.11.
2. The field-marshal fixture executes the documented 16 hot-path JNI API calls.
3. The field-marshal benchmark creates six Java objects per measured request under its deliberately small target contract.
4. The Region fixture uses three hot-path JNI API calls and reusable arrays.
5. Region copy timings increase with payload size in the expected direction.
6. The DirectByteBuffer fixture creates its buffers during setup, verifies the native addresses, and uses one JNI dispatch call per measured request.
7. The DirectByteBuffer no-copy target remains approximately constant with payload size, while the Java-side copy target grows strongly with payload size.
8. All three fixtures passed payload correctness checks and completed 100,000 measured iterations at each tested size.

## What the measurements do not confirm

1. Absolute JNI costs on other CPUs, operating systems, JDK builds or JVM configurations.
2. NativeTomcat end-to-end request latency or throughput.
3. Tomcat `SocketWrapperBase`, `AbstractEndpoint`, protocol parser, Coyote, Catalina or Servlet execution costs.
4. GC impact under a real request mix.
5. Native buffer lifetime, backpressure, cancellation, request-body consumption, response production or thread-affinity correctness in the real NativeTomcat architecture.
6. Whether DirectByteBuffer is faster once real Tomcat processing is included.
7. End-to-end zero-copy. The benchmark only establishes that the JNI bridge itself does not copy the payload when direct buffers are reused.

## Cost comparison

Let:

- `N` = request payload bytes crossing the boundary.
- `M` = response payload bytes crossing the boundary.
- `F` = number of request fields marshalled individually.
- `L` = bytes in strings converted/copied by the bridge.
- `T_Java` = Java/Tomcat/application processing, excluded from the bridge-only theoretical comparison.

| Bridge | Hot-path JNI API calls | Bridge Java allocations/request | Payload copy/request | Asymptotic bridge model |
|---|---:|---:|---:|---|
| Field-marshal | contract-dependent; measured example = 16 | 6 in the measured example | `N + M` plus string conversion | `Θ(F + L + N + M) + T_Java` |
| `byte[]` / Region | 3 | 0 | `N + M` | `3*C_JNI + Θ(N + M) + T_Java` |
| DirectByteBuffer | 1 | 0 | 0 at the JNI bridge | `C_JNI + C_dispatch + T_Java` |

The boundary-only ordering is therefore:

```text
field-marshal  ──────────────── most boundary work
      │
      ▼
byte[] / Region ────────────── fewer calls, still copies payload
      │
      ▼
DirectByteBuffer ───────────── no JNI payload copy; fixed call overhead
```

The benchmark is consistent with this ordering only for the isolated fixture. It is not evidence that production implementation should immediately choose DirectByteBuffer.

## JDK 21 HotSpot source facts that constrain implementation

The model is based on JDK 21 HotSpot JNI implementation, not JNI documentation or third-party summaries. Relevant source-level constraints are:

1. `NewObject` performs Java object allocation and returns a JNI local handle.
2. `Set*Field` operates through JNI handles and uses normal JVM reference-store/barrier machinery for reference fields; it is not a raw C assignment.
3. `Set<Type>ArrayRegion` copies native data into a Java array and `Get<Type>ArrayRegion` copies Java-array data back to native memory. Their payload work therefore grows with region length.
4. `Call<Type>Method*` enters the JVM Java-call machinery; one JNI API call must not be modeled as one machine instruction.
5. JNI local/global references are handles. Zero new user-visible references on a hot path does not imply zero internal JNI handle machinery.

These source facts constrain the asymptotic model but do not provide trustworthy absolute nanosecond values. Actual latency requires controlled measurement on the target JDK/CPU.

## Engineering implications

1. Preserve existing native connection/buffer ownership rules.
2. Prototype the smallest semantically correct bridge first.
3. Prefer reusable `byte[]`/Region as the correctness baseline because its ownership contract is explicit and the benchmark demonstrates a small three-call boundary.
4. Evaluate DirectByteBuffer as the performance-oriented target only after proving buffer lifetime, backpressure, request-body consumption, response production, exception propagation, thread affinity, and Servlet/Tomcat semantics.
5. Treat field-marshal as a compatibility/reference design rather than the default high-throughput path unless the final contract demonstrates that its metadata/object cost is acceptable.
6. Extend the benchmark suite to separately measure JNI-call overhead, allocation, copies, Java target execution and eventual end-to-end request latency.
7. Keep the fixtures under `native/tests/JNI_bridge_test/`; this is the corrected repository path. The former `native/test/JNI_bridge_test/` path is obsolete and contains no benchmark fixture on `main`.

## Non-goals / guardrails

- This document does not authorize changing the current C/Java ABI.
- It does not claim that NativeTomcat already implements any of the three bridges.
- It does not establish Servlet/Tomcat/TCK equivalence.
- It does not turn the microbenchmark results into production performance guarantees.
- It does not equate zero JNI payload copy with end-to-end zero-copy.
