package org.apache.tomcat.nativebootstrap;

import java.util.Objects;

import org.apache.catalina.startup.Bootstrap;

/**
 * Java-side lifecycle bridge invoked by the NativeTomcat C process entry point.
 */
public final class NativeTomcatBootstrap {

	private static Bootstrap bootstrap;

	private NativeTomcatBootstrap() {
	}

	/**
	 * Initialize and start the pinned Tomcat Catalina lifecycle.
	 *
	 * @param args Arguments supplied by the native process
	 * @throws Exception If Tomcat bootstrap or startup fails
	 */
	public static synchronized void bootstrap(String[] args) throws Exception {
		if (bootstrap != null) {
			return;
		}

		String catalinaHome = requireEnvironment("NATIVETOMCAT_CATALINA_HOME");
		String catalinaBase = System.getenv("NATIVETOMCAT_CATALINA_BASE");
		if (catalinaBase == null || catalinaBase.isEmpty()) {
			catalinaBase = catalinaHome;
		}

		System.setProperty("catalina.home", catalinaHome);
		System.setProperty("catalina.base", catalinaBase);

		Bootstrap instance = new Bootstrap();
		instance.init(args == null ? new String[0] : args);
		instance.start();
		bootstrap = instance;
	}

	/**
	 * Stop and release the Tomcat lifecycle previously started by bootstrap().
	 */
	public static synchronized void shutdown() throws Exception {
		if (bootstrap == null) {
			return;
		}

		Bootstrap instance = bootstrap;
		bootstrap = null;
		instance.stop();
	}

	private static String requireEnvironment(String name) {
		return Objects.requireNonNull(System.getenv(name), name + " is required");
	}
}
