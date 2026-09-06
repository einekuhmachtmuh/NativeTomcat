package org.apache.tomcat.nativebootstrap;

import java.lang.reflect.Method;
import java.util.Objects;
import java.util.concurrent.Executor;

/**
 * Java-side lifecycle bridge invoked by the NativeTomcat C process entry point.
 *
 * <p>The current repository intentionally keeps this bridge free of compile-time
 * dependencies on Tomcat implementation classes. Tomcat's Java source is being
 * brought into this repository incrementally so that those classes can be
 * modified and compiled as part of the NativeTomcat build instead of treating
 * an external Tomcat build as an immutable binary dependency.</p>
 */
public final class NativeTomcatBootstrap {

    private static Object bootstrap;
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

        Class<?> bootstrapClass = Class.forName("org.apache.catalina.startup.Bootstrap");
        Object instance = bootstrapClass.getConstructor().newInstance();
        invoke(instance, "init", new Class<?>[] { String[].class },
                new Object[] { args == null ? new String[0] : args });
        invoke(instance, "start", new Class<?>[0], new Object[0]);
        try {
            Executor executor = resolveTomcatExecutor(instance, bootstrapClass);
            eventDispatcher = new NativeEventDispatcher(executor, NativeTomcatBootstrap::processNativeEvent);
            bootstrap = instance;
        } catch (Exception e) {
            try {
                invoke(instance, "stop", new Class<?>[0], new Object[0]);
            } catch (Exception stopFailure) {
                e.addSuppressed(stopFailure);
            }
            throw e;
        }
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

        Object instance = bootstrap;
        try {
            invoke(instance, "stop", new Class<?>[0], new Object[0]);
        } finally {
            bootstrap = null;
        }
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

    private static Executor resolveTomcatExecutor(Object instance, Class<?> bootstrapClass) throws Exception {
        Method getServer = bootstrapClass.getDeclaredMethod("getServer");
        getServer.setAccessible(true);
        Object server = getServer.invoke(instance);
        if (server == null) {
            throw new IllegalStateException("Tomcat server is not available after Bootstrap.start()");
        }

        Object[] services = (Object[]) invoke(server, "findServices", new Class<?>[0], new Object[0]);
        for (Object service : services) {
            Object[] connectors = (Object[]) invoke(service, "findConnectors", new Class<?>[0], new Object[0]);
            for (Object connector : connectors) {
                Object protocolHandler = invoke(connector, "getProtocolHandler", new Class<?>[0], new Object[0]);
                if (protocolHandler == null) {
                    continue;
                }
                Object executor = invoke(protocolHandler, "getExecutor", new Class<?>[0], new Object[0]);
                if (executor instanceof Executor) {
                    return (Executor) executor;
                }
            }
        }
        throw new IllegalStateException("No Tomcat connector Executor is available");
    }

    private static Object invoke(Object target, String name, Class<?>[] parameterTypes, Object[] args) throws Exception {
        Method method = target.getClass().getMethod(name, parameterTypes);
        return method.invoke(target, args);
    }

    private static String requireEnvironment(String name) {
        return Objects.requireNonNull(System.getenv(name), name + " is required");
    }
}
