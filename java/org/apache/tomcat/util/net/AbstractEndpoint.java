package org.apache.tomcat.util.net;

import java.util.concurrent.Executor;

/** Minimal intermediate endpoint surface used to verify SocketWrapperBase dispatch ownership. */
public abstract class AbstractEndpoint<S,U> {
    private final Executor executor;
    protected AbstractEndpoint(Executor executor) { this.executor = executor; }
    public Executor getExecutor() { return executor; }
    public boolean isRunning() { return true; }
    public void execute(Runnable runnable) { if (executor == null) throw new IllegalStateException("executor unavailable"); executor.execute(runnable); }
    public boolean processSocket(SocketWrapperBase<S> wrapper, SocketEvent event, boolean dispatch) {
        Runnable task = () -> { if (wrapper != null && !wrapper.isClosed()) { } };
        if (dispatch) { execute(task); } else { task.run(); }
        return true;
    }
}
