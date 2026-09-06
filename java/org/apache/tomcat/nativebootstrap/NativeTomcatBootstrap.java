package org.apache.tomcat.nativebootstrap;

/**
 * Minimal Java-side bootstrap invoked by the NativeTomcat C process entry point.
 *
 * The native process owns JVM creation. This class intentionally keeps the
 * first milestone small: Tomcat lifecycle integration is added only after the
 * embedded JVM path is verified.
 */
public final class NativeTomcatBootstrap {

	private NativeTomcatBootstrap() {
	}

	/**
	 * Bootstrap hook for the native process.
	 *
	 * @param args Arguments supplied by the native process
	 */
	public static void bootstrap(String[] args) {
		// Tomcat/Catalina integration is intentionally deferred until the
		// embedded JVM lifecycle and class-loading contract are verified.
	}
}
