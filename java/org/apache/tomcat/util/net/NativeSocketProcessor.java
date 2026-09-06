package org.apache.tomcat.util.net;

/** Minimal concrete processor used only to verify the SocketProcessorBase lock boundary. */
public final class NativeSocketProcessor extends SocketProcessorBase<Long> {
    private final Runnable action;
    public NativeSocketProcessor(SocketWrapperBase<Long> wrapper, SocketEvent event, Runnable action) {
        super(wrapper, event);
        this.action = action;
    }
    @Override protected void doRun() { action.run(); }
}
