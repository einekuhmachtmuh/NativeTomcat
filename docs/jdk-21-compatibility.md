# JDK 21 Compatibility Assessment for Tomcat 11.0.25

## 1. Conclusion

JDK 21 is a supported **runtime baseline** for Tomcat 11.0.x and is therefore retained as the current local runtime for NativeTomcat development.

It is **not sufficient for the Tomcat 11.0.25 release build**. The upstream Ant build declares:

- `min.java.version = 17`;
- `build.java.version = 17`;
- `compile.release = 17`;
- `release.java.version = 22`.

The build's Java-version guard therefore accepts JDK 21 for the normal build path, while the release compilation path requests `javac --release 22` and cannot be reproduced with JDK 21.

This distinction replaces the earlier, overly broad statement that JDK 21 was simply "incompatible" with Tomcat 11.0.25.

## 2. Verified runtime compatibility

Apache Tomcat's official documentation states that Tomcat 11.0.x supports Java 17 and later. The Tomcat compatibility implementation also explicitly uses a Java 17 implementation as its base and selects `Jre21Compat` on a Java 21 runtime when Java 22 support is unavailable.

For JDK 21, `JreCompat` therefore does not require the JDK 22 implementation merely to run the ordinary Tomcat code path.

## 3. Concrete JDK 21 limitation: OpenSSL FFM / Panama

Tomcat 11.0.25 contains a separate OpenSSL implementation under `org.apache.tomcat.util.net.openssl.panama` that uses `java.lang.foreign` APIs such as `Arena` and `MemorySegment`.

The official Tomcat SSL documentation explicitly states that this Java FFM OpenSSL implementation requires Java 22 or later. The same documentation also describes JSSE and the traditional Tomcat Native/OpenSSL implementation as alternative SSL paths.

Therefore:

| Component / feature | JDK 21 | JDK 22+ |
|---|---:|---:|
| Ordinary Tomcat 11.0.x runtime | Supported | Supported |
| Servlet 6.1 API/runtime baseline | Supported | Supported |
| Normal Java NIO HTTP connector | Supported | Supported |
| Tomcat JRE compatibility layer | Uses JRE 21 implementation | Uses JRE 22 implementation |
| Java FFM / Panama OpenSSL implementation | **Not available** | **Available** |
| Tomcat 11.0.25 release build | **Not sufficient** | **Required** |

NativeTomcat's first native transport bridge does not depend on the Panama OpenSSL path. TLS is deferred, so this JDK 21 limitation does not block the first plain-TCP HTTP/1.1 milestone.

## 4. Implication for NativeTomcat

The project must not use "JDK 21 incompatible with Tomcat 11.0.25" as an architectural premise.

The correct model is:

```text
JDK 17+       -> Tomcat 11 runtime compatibility floor
JDK 21        -> supported runtime; current local environment
JDK 21        -> sufficient for ordinary source/build path in principle
JDK 21        -> cannot perform Tomcat 11.0.25 release compilation
JDK 21        -> cannot use Tomcat's Java-FFM OpenSSL implementation
JDK 22+       -> required for those release/FFM-specific paths
```

The NativeTomcat core therefore remains JDK 17+ compatible unless a concrete source or build requirement proves otherwise. JNI is retained as the initial C/JVM bridge because it does not depend on the Java 22 FFM API.

## 5. Verification gate

A JDK 21 build result must be reported separately from a release-build result. Once a source tree is available on a build host, the following must be tested independently:

1. ordinary Tomcat Ant compilation under JDK 21;
2. Tomcat tests under JDK 21;
3. NativeTomcat JNI compilation and loading under JDK 21;
4. Tomcat 11.0.25 release build under JDK 22+;
5. Panama/OpenSSL path under JDK 22+;
6. the same functional tests under JDK 21 and JDK 22+ where both configurations are supported.

No successful result is claimed for these gates until they are actually executed.
