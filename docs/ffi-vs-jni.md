# FFM vs JNI Decision for NativeTomcat

## 1. Decision

NativeTomcat will use **JNI for the first C/JVM transport bridge**.

Java FFM remains an optional future experiment, not a required dependency and not a parallel production path at this stage.

The decision is based on the actual NativeTomcat boundary rather than on a generic claim that JNI or FFM is universally faster.

## 2. What the boundary actually needs

The first bridge needs to:

- load and call native C transport functions;
- carry an opaque native connection handle;
- create or consume direct `ByteBuffer` views over native storage where a no-copy path is safe;
- propagate I/O errors and close state into Java;
- let native events trigger Java-side transport processing;
- preserve Java/Tomcat ownership of Servlet, HTTP and processor semantics;
- support a Java 17+ runtime floor without making Java 22-specific APIs a core dependency.

This is a relatively small, stable native-method boundary. It is not primarily a general-purpose binding generator problem.

## 3. FFM advantages

FFM has real advantages and should not be dismissed merely because JNI is familiar:

1. **Less handwritten JNI glue.** Java can describe foreign function signatures and memory layouts directly.
2. **Better Java-side safety model.** `MemorySegment`/`Arena` provide explicit lifetime and accessibility rules instead of relying on raw JNI native pointers.
3. **Better maintainability for broad C APIs.** FFM becomes especially attractive when many C functions and C data structures must be bound.
4. **Performance goal.** OpenJDK's FFM design explicitly targets performance comparable to or better than JNI, so FFM should not be rejected on an assumption that it is slower.
5. **Pure-Java binding layer.** A large amount of mechanically generated JNI wrapper code can be avoided.

These are architectural/productivity advantages. They do not establish a NativeTomcat performance advantage without measurement.

## 4. JNI advantages for this project

For the first NativeTomcat boundary, JNI has stronger practical advantages:

1. **Java-version compatibility.** JNI is part of the long-standing Java native interface and does not require the finalized Java 22 FFM API.
2. **Direct-buffer interoperability.** JNI directly provides `NewDirectByteBuffer`, `GetDirectBufferAddress` and `GetDirectBufferCapacity`. This maps naturally to the planned native-buffer lease model.
3. **Native embedding compatibility.** NativeTomcat already needs a native process that embeds/starts the JVM; JNI's Invocation API is therefore relevant to the architecture independently of ordinary Java-to-C calls.
4. **Small surface.** The initial adapter needs only a small number of native entry points. FFM's broader type/memory model is not required merely to call these functions.
5. **Mature Tomcat integration path.** A Java class declaring `native` methods can be integrated without introducing `java.lang.foreign` into the Servlet/Tomcat runtime path.

## 5. Performance conclusion

There is **no verified evidence at this stage that FFM provides a material performance advantage for NativeTomcat's intended boundary**.

OpenJDK's FFM design goal is performance comparable to or better than JNI, but that is not equivalent to a measured advantage for this workload. The relevant workload is dominated by socket readiness, kernel I/O, HTTP processing, buffering, Java dispatch and application execution. A crossing-cost microbenchmark alone cannot establish end-to-end superiority.

Therefore the project will not add FFM solely for a presumed lower call overhead.

If later profiling shows that the JNI boundary itself is a measurable bottleneck, an isolated FFM implementation can be benchmarked against the same JNI implementation using identical JVM, compiler, CPU, payload and call patterns.

## 6. Safety and lifetime conclusion

FFM's explicit memory-session/lifetime model is attractive, but NativeTomcat must already implement explicit ownership rules because the native event loop and Java processor can outlive an individual method call.

Consequently, adopting FFM would not remove the need for a NativeTomcat connection lifetime protocol. The project still needs:

```text
ACTIVE
  -> JAVA_DISPATCHED / I/O_PENDING
  -> CLOSING
  -> CLOSED
```

and must prevent Java from accessing a native buffer after that buffer has been reclaimed.

FFM may make some of these rules easier to express in Java, but it does not eliminate the underlying concurrency/lifetime problem.

## 7. Decision matrix

| Criterion | JNI | FFM | NativeTomcat decision |
|---|---|---|---|
| Java 17+ compatibility | Strong | Final API starts at Java 22 | JNI |
| Direct `ByteBuffer` bridge | Directly supported | Possible through `MemorySegment`/buffer interop | JNI initially |
| Small number of native calls | Simple enough | More abstraction than required | JNI |
| Large C API binding | More glue | Strong | Re-evaluate later |
| Memory lifetime expression | Manual discipline | Stronger Java-side model | JNI + explicit project contract |
| Native embedding/JVM startup | Relevant | Not a replacement for Invocation API | JNI |
| Expected call performance | Mature | Designed for comparable/better performance | Measure before switching |
| Evidence of material end-to-end advantage here | None yet | None yet | JNI |
| Core dependency on JDK 22+ | No | Yes for finalized FFM | Avoid for first milestone |

## 8. Version-policy consequence

The project will **not continue spending implementation effort on JDK-version compatibility solely because FFM exists**.

JDK 21's lack of the finalized FFM API is therefore treated as a non-blocking architectural fact rather than a NativeTomcat compatibility problem.

JDK 22+ will only become a required development/runtime baseline if a later NativeTomcat feature independently requires a JDK 22+ API and that requirement is accepted deliberately.

## 9. Reconsideration trigger

Reconsider FFM only if at least one of these becomes true:

- JNI crossing overhead is demonstrated by profiling to be a material end-to-end bottleneck;
- the native boundary expands enough that handwritten JNI glue becomes a significant maintenance risk;
- FFM materially simplifies a required memory/lifetime design without imposing unacceptable compatibility cost;
- a benchmark on the target workload demonstrates a statistically meaningful performance or scalability improvement.

Until then, JNI is the project baseline.
