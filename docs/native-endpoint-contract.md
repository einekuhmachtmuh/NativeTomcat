# NativeEndpoint Contract Audit

本文件記錄建立 `NativeEndpoint` 前，對 pinned Tomcat 11.0.25 `AbstractEndpoint`、`NioEndpoint` 與 NGINX event layer 的 source-first contract audit。它不是新的 Servlet/Tomcat 規格；若後續 source evidence 推翻目前結論，必須更新本文件與 roadmap。

## 1. 固定基準

- Tomcat 11.0.25
- pinned commit: `cbe6e15ee81e2fc6232954292a80cca5d1e84009`
- NativeTomcat `AbstractEndpoint.java` verified blob: `85f1c9e23464e17ae14108ae4b04fbf4dad07cdd`
- NativeTomcat `NioEndpoint.java` verified blob: `21b0cadbbb3ab04351415c0d7c127ab3500ace58`
- `SocketWrapperBase.java` 已完成 pinned source migration；目前為 format-normalized source，不宣稱 byte-identical。

## 2. `AbstractEndpoint` 已確認的 integration contract

### 2.1 `processSocket()` 是既有的 dispatch boundary

Pinned source 的 `processSocket(SocketWrapperBase<S>, SocketEvent, boolean)`：

1. null wrapper 直接失敗；
2. 從 `processorCache` 取 `SocketProcessorBase`；
3. 沒有可重用 processor 時呼叫 `createSocketProcessor()`；
4. reset processor 的 socket/event；
5. 取得 endpoint executor；
6. `dispatch && executor != null` 時 enqueue 到 executor；否則直接 `run()`；
7. `RejectedExecutionException` 與其他 Throwable 轉為失敗並回傳 `false`；成功則回傳 `true`。

因此 Native event dispatcher 不應自己重做 SocketProcessor/executor semantics；它應解析 stable handle 後進入 endpoint 的 `processSocket()`。

### 2.2 已直接確認的 abstract methods

目前從 pinned `AbstractEndpoint.java` 直接確認的 abstract contract：

```text
bind() throws Exception
startInternal() throws Exception
stopInternal() throws Exception
getLog()
createSocketProcessor(SocketWrapperBase<S>, SocketEvent)
doCloseServerSocket() throws IOException
serverSocketAccept() throws Exception
setSocketOptions(U)
destroySocket(U)
```

其中 `unbind()` 並非 abstract；其 base implementation 負責清理 generated SSLContext 狀態。因此 NativeEndpoint 不應為了「完整性」自行重寫這段 lifecycle semantics。

### 2.3 Lifecycle ownership

`AbstractEndpoint` 的 final lifecycle 為：

```text
init()
  └─ optional bindWithCleanup()

start()
  └─ bindWithCleanup() if needed
  └─ startInternal()

pause()/resume()

stop()
  └─ stopInternal()
  └─ unbind() when appropriate

destroy()
```

因此 NativeEndpoint 的 native epoll/runtime 啟停必須掛在 `bind/startInternal/stopInternal/doCloseServerSocket` 等既有 contract 上，而不能由 `NativeSocketWrapper` 自己控制 endpoint lifecycle。

## 3. NIO source 對照

`NioEndpoint` 是 semantic reference，不是 NativeEndpoint 的 superclass implementation。

Pinned source 中：

```text
NioEndpoint
  extends AbstractNetworkChannelEndpoint<NioChannel, SocketChannel>

setSocketOptions()
  -> create/reuse NioChannel
  -> create NioSocketWrapper
  -> connections.put(socket, wrapper)
  -> configure non-blocking
  -> Poller.register(wrapper)
```

Poller 的 readiness path：

```text
Selector.select()
  -> selected SelectionKey
  -> processKey()
  -> unreg(readyOps)
  -> processSocket(wrapper, OPEN_READ/OPEN_WRITE, true)
```

`processKey()` 明確先處理 READ，再處理 WRITE；processor dispatch 成功與否會影響後續 close decision。

這證明 NativeEndpoint 需要保留的是：

- connection registry
- `SocketWrapperBase` identity
- `SocketEvent` mapping
- `processSocket()` dispatch contract
- close/error propagation
- processor/executor semantics

而不是複製 Java Selector/Poller。

## 4. NativeEndpoint 的明確 non-goals

第一個 NativeEndpoint gate 不得：

- 建立 `Selector` / `SelectionKey`；
- 讓同一 native fd 同時由 Java Selector 與 native epoll ownership；
- 把 kernel readiness 直接當 Servlet `isReady()`；
- 自行實作另一套 SocketProcessor executor；
- 把 `NativeSocketWrapper` 當 native event-loop owner；
- 在 transport consumption 尚未建立前進入 `Http11Processor` 或 Servlet integration。

## 5. NGINX cross-check

NGINX official event source 將 event backend 與 handler processing 分層：

```text
kernel readiness
  -> ngx_process_events()
  -> event handler
  -> ngx_handle_read_event()/ngx_handle_write_event()
```

`ngx_event_actions_t` 將 add/del/enable/disable/process_events/notify 等 backend operation 集中在 event abstraction；epoll backend 使用 read/write event registration，而 `ngx_handle_read_event()` / `ngx_handle_write_event()` 負責更新 interest state。

NativeTomcat 因此採同一 ownership principle：native event loop 擁有 fd registration/rearm，Java worker 只處理 transport/protocol semantics；Java 不直接修改 event-loop-owned epoll state。

## 6. 下一個 coding gate

在建立 `NativeEndpoint.java` 前，還必須完成兩項 source verification：

1. 從 pinned `AbstractEndpoint` 完整列出所有 abstract/override contract，以及 `AbstractNetworkChannelEndpoint` 對 address/channel contract 的要求；
2. 從 `NioEndpoint` 完整列出 `createSocketProcessor()`、connection registration/removal、close/recycle 與 executor interaction，並逐項標示 NativeEndpoint 的 native-owned / Java-owned responsibility。

完成上述兩項後，才能建立第一版 NativeEndpoint。第一版仍只建立 real `SocketWrapper -> processSocket -> SocketProcessorBase` 的 integration boundary，不實作完整 read/write/TLS/sendfile。

## 7. Verification status

- source-verified: `AbstractEndpoint` pinned blob and relevant lifecycle/processSocket sections
- source-verified: `NioEndpoint` pinned blob and Poller/processKey/processSocket path
- architecture cross-check: official NGINX event source
- implemented: 尚未建立 NativeEndpoint
- compiled: 尚未驗證最新 repo
- unit/integration/TCK/benchmark: 尚未驗證
- local full build: blocked by目前 execution environment 無法解析 GitHub，且本地沒有可用 `.git` working tree
