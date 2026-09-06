package org.apache.tomcat.nativebootstrap;

import java.util.Objects;
import java.util.concurrent.Executor;

import org.apache.catalina.Server;
import org.apache.catalina.Service;
import org.apache.catalina.connector.Connector;
import org.apache.catalina.startup.Bootstrap;
import org.apache.coyote.ProtocolHandler;

/**
 * Java-side lifecycle bridge invoked by the NativeTomcat C process entry point.
 */
public final class NativeTomcatBootstrap {

	private static Bootstrap bootstrap;
	private static NativeEventDispatcher eventDispatcher;

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
		eventDispatcher = new NativeEventDispatcher(resolveTomcatExecutor(instance),
				NativeTomcatBootstrap::processNativeEvent);
		bootstrap = instance;
	}

	/**
	 * Stop and release the Tomcat lifecycle previously started by bootstrap().
	 */
	public static synchronized void shutdown() throws Exception {
		if (bootstrap == null) {
			return;
		}

		NativeEventDispatcher dispatcher = eventDispatcher;
		eventDispatcher = null;
		if (dispatcher != null) {
			dispatcher.stop();
		}

		Bootstrap instance = bootstrap;
		instance.stop();
		bootstrap = null;
	}

	/** Called by the native event loop after attaching its thread to the JVM. */
	public static void dispatchNativeEvent(long connectionHandle, int events) {
		NativeEventDispatcher dispatcher = eventDispatcher;
		if (dispatcher == null) {
			return;
		}
		dispatcher.dispatch(connectionHandle, events);
	}

	private static void processNativeEvent(long connectionHandle, int events) {
		// This task is already running under Tomcat's Executor ownership. The
		// native transport-to-SocketWrapper hand-off will invoke the real
		// SocketProcessor-compatible processing layer from here.
	}

	private static Executor resolveTomcatExecutor(Bootstrap instance) throws Exception {
		java.lang.reflect.Method getServer = Bootstrap.class.getDeclaredMethod("getServer");
		getServer.setAccessible(true);
		Server server = (Server) getServer.invoke(instance);
		if (server == null) {
			throw new IllegalStateException("Tomcat server is not available after Bootstrap.start()");
		}
		for (Service service : server.findServices()) {
			for (Connector connector : service.findConnectors()) {
				ProtocolHandler protocolHandler = connector.getProtocolHandler();
				Executor executor = protocolHandler.getExecutor();
				if (executor != null) {
					return executor;
				}
			}
		}
		throw new IllegalStateException("No Tomcat connector Executor is available");
	}

	private static String requireEnvironment(String name) {
		return Objects.requireNonNull(System.getenv(name), name + " is required");
	}
}
