# Tomcat、NGINX 與預想中的 NativeTomcat：元件化逐流程比較

> 本文件是對 `docs/`（不含 `docs/work-principles.md`）目前設計、稽核與 implementation gate 的整合性技術總覽。它不是把歷史文件逐篇翻譯，而是將其共同結論重新組織成「依元件、沿流程」的比較模型。
>
> **證據原則**：Tomcat 行為以 Apache Tomcat 11.0.25 pinned source `cbe6e15ee81e2fc6232954292a80cca5d1e84009` 為準；NGINX 以官方 1.30.4 `release-1.30.4` event source 為架構交叉核對；NativeTomcat 欄位區分「目前已實作/已 source-verified」與「預想目標」，不得把設計意圖寫成已完成語意。

## 0. 三者的根本定位

| 層次 | Apache Tomcat | NGINX | 預想中的 NativeTomcat |
|---|---|---|---|
| 核心責任 | Servlet container、HTTP connector、protocol/application lifecycle | native event-driven network server | 保留 Tomcat Servlet/application semantics，將可證明有成本優勢的 network/I/O/runtime path native 化 |
| kernel I/O owner | Java NIO `Selector` / `Poller` | native event module / epoll backend | native runtime event-loop |
| connection object | `SocketWrapperBase` / `NioSocketWrapper` | `ngx_connection_t` | `NativeSocketWrapper` + stable native handle |
| readiness | `SelectionKey.readyOps()` | `epoll_wait()` / event flags | native `epoll` readiness |
| application dispatch | `AbstractEndpoint.processSocket()` → `SocketProcessorBase` → protocol handler | event handler → core/HTTP handler | 真正的 `AbstractEndpoint.processSocket()` → `SocketProcessorBase` → Tomcat protocol handler |
| rearm / interest owner | NIO `Poller` / selector thread | NGINX event layer | native event-loop owner |
| Servlet semantics | 原生責任 | 不存在 | 必須保留 Tomcat/Jakarta Servlet 語意 |

最重要的共同抽象不是「都使用 epoll」，而是：

```text
kernel readiness
    → event owner
    → connection/transport state
    → protocol processing
    → application-visible semantics
    → next interest / close
```

三者在這條鏈上的責任分配不同，因此不能因為某一層的 API 名稱相似就宣稱語意等價。

---

# 第一章　Process、Endpoint 與啟動生命週期

## 1.1 Tomcat

Tomcat 的 endpoint 是 Java 元件生命週期的一部分。`AbstractEndpoint` 管理 endpoint lifecycle、connection registry、processor cache、executor、socket properties 等；`NioEndpoint` 再把這些抽象具體化為 Java NIO channel、selector/poller 與 `NioSocketWrapper`。

概念流程：

```text
Tomcat/Catalina 啟動
    → ProtocolHandler
    → Endpoint.init()
    → Endpoint.start()
    → bind / accept infrastructure
    → Poller + Acceptor
    → ready connections
```

`AbstractEndpoint.processSocket()` 是後續 dispatch 的正式入口，而不是 Native endpoint 自己複製一份 executor/processor 邏輯。

## 1.2 NGINX

NGINX 以 process/worker + event module 為核心。`ngx_event_process_init()` 初始化所選 event module、timer、connection/event 結構與 listening event；worker 後續透過 `ngx_process_events_and_timers()` 驅動 event loop。

官方 source：

- `src/event/ngx_event.c`
- `src/event/ngx_event.h`
- `src/event/modules/ngx_epoll_module.c`

NGINX 沒有 Tomcat 式 Servlet container lifecycle，也沒有 `SocketProcessorBase` 等 Java executor contract。

## 1.3 NativeTomcat 目標

NativeTomcat 不應把 Tomcat endpoint lifecycle 改寫成 NGINX lifecycle。預想分工為：

```text
Tomcat Java lifecycle
    → NativeEndpoint
    → native runtime bind/start
    → native event-loop / accept owner
    → Java wrapper/protocol layer
```

Native runtime 負責 native socket/event-loop lifetime；Java endpoint 仍負責 Tomcat endpoint contract。兩者間必須有明確 ownership 與 shutdown ordering。

**目前 gate：** endpoint/processor 的完整 real integration 尚未證明；因此不能把 NativeTomcat 寫成已經完成 Tomcat endpoint equivalence。

---

# 第二章　Listener、Bind 與 Accept

## 2.1 Tomcat NIO

Tomcat NIO 的 endpoint 由 Java channel/server socket 建立 listening endpoint，再由 `Acceptor` 呼叫 endpoint 的 accept path。accepted channel 被包成 NIO transport/wrapper，加入 endpoint connection registry，並交由 Poller 管理 readiness。

核心概念：

```text
listen
  → accept
  → NioChannel
  → NioSocketWrapper
  → Poller registration
  → connection registry
```

## 2.2 NGINX

NGINX 在 event initialization 階段為 listening socket 建立 accept event；stream socket 的 read event 可指向 `ngx_event_accept`。依 worker/reuseport/accept-mutex/exclusive-accept 設定，listen event 的 kernel registration 由 event layer 建立。

NGINX 的 accept event 是 native event handler；沒有 Tomcat 的 Java wrapper/Servlet boundary。

## 2.3 NativeTomcat 目標

預想流程：

```text
native listener
  → native event loop
  → accept()
  → stable uint64 native handle
  → NativeSocketWrapper
  → Java endpoint connection registry
  → initial READ interest
```

native accept 與 native connection lifetime 應由 native runtime 擁有；Java 不應直接取得 native event-loop ownership。

**關鍵 invariant：** native handle 必須是穩定、可驗證、與 native registry lifetime 綁定的 identity；不得只依賴暫時 Java map/callback。

---

# 第三章　Connection Identity、Registry 與 Lifetime

## 3.1 Tomcat

`AbstractEndpoint` 維護 connection registry，`SocketWrapperBase` 維護 wrapper-level state，包括 close/error、timeout、buffer、processor、endpoint reference、read/write state 等。NIO wrapper 的生命週期與 channel 關閉、endpoint registry、processor recycle 相互關聯。

close 不是單純 `fd close`：它必須同步處理 wrapper、endpoint registry、buffer、pending state、processor/handler 等 Tomcat state。

## 3.2 NGINX

NGINX 使用 `ngx_connection_t` 與 `ngx_event_t` 組合管理 connection/event state。`ngx_event_t` 有 `instance` bit，用於辨識 kqueue/epoll 的 stale event。event 結構亦明確保存 active、ready、eof、error、closed 等狀態。

這裡的重點不是複製 NGINX 結構，而是其 source 顯示：**kernel event identity 與 connection lifetime 必須有可驗證的 stale-event 防護。**

## 3.3 NativeTomcat 目標

NativeTomcat 使用：

```text
stable uint64 handle
    ↕
native active-connection registry
    ↕
NativeSocketWrapper
```

native event 不直接把可能已失效的 `nt_connection_t *` 當作長期 identity；event 帶 stable handle，再由 native registry 驗證目前仍有效後才取得 connection object。

這與 NGINX 的 stale-event protection 是架構上的交叉核對，但不能宣稱兩者實作相同。

---

# 第四章　Kernel Readiness 與 Event Polling

## 4.1 Tomcat NIO

Tomcat NIO 的 `Poller` 以 Java `Selector` 為 readiness owner。`processKey()` 讀取 `readyOps()` 後，先消費當前 readiness，再分別處理 READ 與 WRITE。READ 與 WRITE 是獨立方向；同一次 readiness notification 可以形成兩個 processor dispatch。

因此：

```text
kernel/Selector readiness
    ≠ SocketEvent
    ≠ ServletInputStream.isReady()
```

## 4.2 NGINX

NGINX 的 event abstraction 將 event polling 與 read/write registration 分開。`ngx_handle_read_event()` 與 `ngx_handle_write_event()` 分別處理兩個方向；epoll backend 目前使用 `EPOLLIN | EPOLLRDHUP` 表示 read readiness、`EPOLLOUT` 表示 write readiness，並使用 NGINX 的 clear-event/edge-triggered 模式。

NGINX 官方 `ngx_event.h` 也明確區分 `active`、`ready`、`oneshot`、`eof`、`error` 等 event state。

## 4.3 NativeTomcat

NativeTomcat 使用 Linux `epoll`；accepted connection 採 `EPOLLONESHOT`，listener/wakeup 使用不同的 native monitoring semantics。

`EPOLLONESHOT` 是 NativeTomcat 自己的 ownership/lifecycle 選擇，不是 NGINX 的實作要求。

核心流程：

```text
epoll_wait()
    → stable handle + event mask
    → native event dispatcher
    → Java transport/wrapper
```

**不可混淆：** `EPOLLIN` 只表示 kernel readiness，不代表資料已被 Tomcat 消費，更不代表 Servlet `isReady()` 已經成立。

---

# 第五章　Readiness → Java Dispatch

## 5.1 Tomcat

Tomcat NIO `Poller.processKey()` 在完成 readiness state handling 後，對 READ/WRITE 呼叫：

```text
AbstractEndpoint.processSocket(wrapper, OPEN_READ/OPEN_WRITE, true)
```

`AbstractEndpoint.processSocket()`：

1. 從 `processorCache` 取得或建立 `SocketProcessorBase`；
2. 必要時 reset；
3. `dispatch=true` 且 executor 存在時提交到 endpoint executor；
4. 否則同步 `run()`；
5. submission/rejection/failure 有既定處理。

`SocketProcessorBase.run()` 是 final contract；它取得 wrapper lock、檢查 closed，再決定是否進入 `doRun()`，最後釋放 lock。

因此「executor submission 成功」不等於「processor 已經完成」。

## 5.2 NGINX

NGINX 事件迴圈把 kernel event 交給 event handler。handler 可能進入 connection/core/HTTP processing；NGINX 沒有 Tomcat 式 `SocketProcessorBase` executor completion boundary。

因此 NGINX 只能證明「event polling 與 handler/interest management 應分層」，不能用來決定 NativeTomcat 的 Java executor lifecycle。

## 5.3 NativeTomcat

預想正確邊界：

```text
native epoll event
    → stable handle
    → NativeEndpoint
    → real AbstractEndpoint.processSocket()
    → real SocketProcessorBase
    → transport/protocol consumption
```

目前 online repo 最新 gate 已確認 `NativeEndpoint.processNativeEvent()` 對 ERROR/READ/WRITE 使用 `processSocket(..., false)`；依 pinned Tomcat contract，這會同步執行 `SocketProcessorBase.run()`，因此**目前尚未建立真正的 Tomcat executor completion boundary**。

這是目前最重要的 integration gate 之一：不能因為存在 `NativeEventDispatcher` 就假定它已是 active completion boundary。

---

# 第六章　SocketWrapper 與 Transport Consumption

## 6.1 Tomcat `SocketWrapperBase`

`SocketWrapperBase` 是 Tomcat transport/application 之間的核心 contract。它統一管理：

- read/write buffer；
- blocking/non-blocking I/O；
- read/write interest registration；
- timeout；
- processor association；
- close/error state；
- SSL/sendfile/vectored I/O 等擴充 contract。

NIO 實作 `NioSocketWrapper` 再透過 channel 執行實際 transport I/O。

## 6.2 NGINX

NGINX 沒有 `SocketWrapperBase`。其 connection/event/buffer 模型較接近：

```text
ngx_connection_t
    ↕
ngx_event_t
    ↕
ngx_buf_t / chain
    ↕
protocol/HTTP handlers
```

NGINX buffer/connection 是 native server architecture，不能直接視為 Servlet transport abstraction。

## 6.3 NativeTomcat

`NativeSocketWrapper` 的目標不是建立另一套 Servlet socket abstraction，而是讓真正的 `SocketWrapperBase` contract 由 native transport 實作。

目前 source audit 已建立的重要原則：

1. Tomcat 的 wrapper buffer semantics 必須保留；不能因為 native `read(2)`/`write(2)` 存在就繞過 Tomcat buffers。
2. `isReadyForRead()` 不能簡化成「socket 未關閉」；它必須遵守 Tomcat/NIO 的 buffered-data + non-blocking fill semantics。
3. EOF、WOULD_BLOCK、fatal error 必須區分。
4. close 必須同時處理 Java wrapper state 與 native connection lifetime。
5. sendfile、TLS、vectored async I/O 等未完成 contract 不得用 fake implementation 消除 `UnsupportedOperationException`。

---

# 第七章　Blocking I/O、Non-blocking I/O 與 Wait/Wakeup

## 7.1 Tomcat NIO

Tomcat NIO 的 blocking wrapper 操作並不是把 Java worker 永久阻塞在 kernel `read()`。當 non-blocking channel 暫時無法進展時，wrapper 進入 blocking state、register interest、等待對應 lock/wakeup，再重試 transport operation。

因此 blocking semantics 本身仍由 Tomcat wrapper lifecycle 管理。

## 7.2 NGINX

NGINX 的核心架構是 event-driven non-blocking I/O。read/write handler 在 readiness 下消費資料，遇到暫時無法進展時保留 event/connection state。

NGINX 可作為「不要讓 worker 直接做長時間 blocking socket operation」的 native architecture cross-check，但它不提供 Tomcat blocking-wrapper contract。

## 7.3 NativeTomcat

NativeSocketWrapper 的設計目標：

```text
Java blocking read
    → native non-blocking read
    → WOULD_BLOCK
    → register READ interest
    → Java wait
    → native readiness
    → wake waiter
    → retry native read
```

這與直接讓 Java thread 呼叫 blocking native `read()` 不同。

同樣地，write path 必須保持獨立 WRITE state。READ waiter 與 WRITE waiter 不得互相覆蓋 interest。

---

# 第八章　Interest、Rearm 與 Event-loop Ownership

## 8.1 Tomcat NIO

Tomcat NIO 的 interest registration 是 additive semantics。`registerReadInterest()` 不代表「清除 WRITE」；Poller 以既有 `interestOps` 加上新 interest 的方式維持另一方向 state。

因此 READ/WRITE state 是獨立的 logical state。

## 8.2 NGINX

NGINX 的 `ngx_handle_read_event()` / `ngx_handle_write_event()` 也是兩個獨立 event state API；epoll backend 對 READ 與 WRITE 分別註冊/修改。

這是 NativeTomcat 必須保留 READ/WRITE 分離的強力架構交叉核對。

## 8.3 NativeTomcat

早期 NativeTomcat 曾以 `want_write` boolean 表示 interest，這與 Tomcat additive READ/WRITE semantics 不一致。online repo 已經將 native rearm API 改為完整 interest mask，並在 Java/native boundary 傳遞完整 mask。

因此目標應為：

```text
desired READ | desired WRITE
        ↓
Java wrapper state
        ↓
NativeTransport.rearm(full mask)
        ↓
native command queue
        ↓
native event-loop owner
        ↓
epoll_ctl(EPOLL_CTL_MOD)
```

### Exactly-one 尚未完成

即使 command queue 對同一批 REARM/CLOSE 做 coalescing，也不能因此證明每個 readiness epoch 恰好只有一次有效 `epoll_ctl()`。

原因是：

```text
queue coalescing
    ≠ stale-command suppression
    ≠ exactly-one effective final application
```

Java processor 可能先後提交多個 command，而 native event loop 可能在不同 wake/command batch 中處理它們。因此必須建立 readiness epoch 與 stale finalization suppression，才能進一步證明 exactly-one effective REARM/CLOSE。

---

# 第九章　Readiness Epoch 與 Processor Completion

## 9.1 Tomcat 限制

一次 NIO readiness notification 可以同時包含 READ 與 WRITE；Tomcat 可以為兩個方向各自提交 processor。`SocketProcessorBase.run()` 又是完整 Runnable completion boundary，而不是只有 `doRun()`。

因此不能：

- 把 READ+WRITE 強行合成一個 processor；
- 只在 `doRun().finally` 當完成；
- 把 executor submission 當完成；
- 讓每個 processor 直接 rearm。

## 9.2 NGINX 對照

NGINX 保持 READ/WRITE event state 獨立，但沒有 Java executor completion boundary。因此它只能支持「獨立方向 state」與「event owner/handler 分離」這兩個部分。

## 9.3 NativeTomcat 預想 epoch

每次 native readiness notification 建立 per-connection epoch：

```text
epoch sequence
consumed READ/WRITE bits
expected processor count
completed processor count
cancelled/closed state
finalized state
```

READ-only → 1 completion；WRITE-only → 1；READ+WRITE → 最多 2 個獨立 processor completion。

完成最後一個 processor 後：

```text
snapshot desired interest
    → mark epoch finalized
    → enqueue final command(epoch, desired mask)
```

若 wrapper 已 closed：

```text
CLOSE
```

而不是 REARM。

舊 epoch 的 finalization command 必須由 native owner 以 sequence/epoch 規則拒絕，避免舊狀態覆寫新狀態。

**狀態：** 這仍是 `theoretical analysis / source-verified design`，不是已完成 runtime proof。

---

# 第十章　HTTP Protocol Processing

## 10.1 Tomcat

真正 HTTP/1.1 path 的概念鏈：

```text
SocketProcessorBase
    → AbstractProtocol.ConnectionHandler
    → Http11Processor
    → Http11InputBuffer / Http11OutputBuffer
    → CoyoteAdapter
    → Catalina
    → Servlet
```

`ConnectionHandler` 根據 `SocketState` 處理 OPEN、CLOSED、LONG、ASYNC、UPGRADE 等狀態，並負責 processor recycle/association。

`Http11Processor` 接受 generic `SocketWrapperBase<?>`；這證明 HTTP processor 的 wrapper type entry point 不必是 `NioSocketWrapper`，但**不等於 native transport 已經相容**。

## 10.2 NGINX

NGINX HTTP handler 直接建立在 native connection/event/buffer model 上，HTTP request parsing、configuration、upstream/filter/output chain 均屬 NGINX 自己的 native HTTP architecture。

它沒有 Servlet container、Filter、Listener、Session、ClassLoader 或 Catalina lifecycle。

## 10.3 NativeTomcat

NativeTomcat 不應重寫 HTTP parser/application semantics 以模仿 NGINX。目標是：

```text
native transport
    → real Tomcat SocketWrapperBase
    → real SocketProcessorBase
    → real ConnectionHandler
    → real Http11Processor
    → real CoyoteAdapter/Catalina/Servlet
```

只有當這條鏈真正建立並通過 integration test 後，才可宣稱 HTTP/Servlet compatibility progress。

---

# 第十一章　Response、Write Interest 與 Back-pressure

## 11.1 Tomcat

Tomcat output 先經 `SocketWrapperBase` / `WriteBuffer` 等 transport buffer，再由 protocol output layer 產生 HTTP encoding/chunking 等資料。當 socket 暫時無法接受更多資料時，WRITE interest 成為後續 transport progress 的機制。

因此 `EPOLLOUT` 不應被永久保持；只有真的有 pending output 且 transport 需要 writable readiness 時才應保留。

## 11.2 NGINX

NGINX 同樣將 writable event 與 output buffer state 分開。`ngx_handle_write_event()` 管理 WRITE event registration；HTTP handler 本身不是 epoll ownership owner。

## 11.3 NativeTomcat

目標：

```text
Tomcat output buffer has pending data
    → native non-blocking write
    → partial/WOULD_BLOCK
    → retain WRITE desired bit
    → native event-loop rearm
    → EPOLLOUT
    → resume transport consumption
    → drain pending output
    → remove WRITE desired bit when no longer needed
```

這避免 writable-socket busy loop。

---

# 第十二章　Close、EOF、Half-close、Error 與 Recycle

## 12.1 Tomcat

Tomcat 必須區分：EOF、I/O error、peer close、wrapper close、protocol CLOSED、keep-alive、upgrade、async 等狀態。`SocketWrapperBase.close()`/`doClose()` 不只是 native descriptor close。

## 12.2 NGINX

NGINX `ngx_event_t` 明確保存 `eof`、`error`、`pending_eof`、`closed` 等 state；event layer 與 connection lifecycle 協同決定後續 handler 行為。

## 12.3 NativeTomcat

native epoll 將 `EPOLLRDHUP` / `EPOLLHUP` / `EPOLLERR` 映射成 native transport events，但不可直接把它們全部轉成「立即 close」。

預想流程：

```text
EPOLLRDHUP / HUP / ERR
    → native event state
    → Java wrapper
    → Tomcat protocol determines semantics
    → close / keep-alive / EOF handling
    → native owner applies final close or interest
```

目前已知 invariant：Java worker 不直接 destroy native connection；close 必須回到 native owner。

---

# 第十三章　Timeout 與 Timer

## 13.1 Tomcat

Tomcat NIO Poller 同時負責 readiness 與 timeout-related polling/lifecycle work；wrapper 具有 read/write timeout state。blocking wait 的 timeout accounting 是 wrapper semantics 的一部分。

目前 source audit 沒有證明 NativeTomcat 有 timeout arithmetic defect；不能以舊有猜測取代最新 source verification。

## 13.2 NGINX

NGINX event layer 有 timer subsystem，並由 `ngx_process_events_and_timers()` 將 timer 與 event polling 協同處理。

## 13.3 NativeTomcat

native runtime 的 event loop 可以成為 future timer owner，但 timer semantics 最終必須映射回 Tomcat wrapper/protocol lifecycle。

第一階段不應為了「功能完整」提前加入複雜 timeout architecture；應先完成 transport → processor → protocol 的主路徑。

---

# 第十四章　TLS、sendfile、vectored I/O 與進階功能

這些功能不應混入第一個 transport gate。

| 功能 | Tomcat | NGINX | NativeTomcat 初期策略 |
|---|---|---|---|
| TLS | JSSE/OpenSSL 等 connector integration | native TLS integration | deferred |
| sendfile | NIO endpoint/protocol integration | native sendfile/event integration | deferred |
| vectored I/O | wrapper/channel contract | native buffer/vector path | deferred |
| HTTP/2 | Tomcat protocol implementation | NGINX HTTP/2 | 第二階段 |
| Async Servlet | Servlet/Tomcat async lifecycle | 無 Servlet 對應 | 第二階段 |
| zero-copy | transport-dependent | native optimization | 只有 benchmark/成本模型證明後 |
| back-pressure | Tomcat buffer/processor semantics | native event/buffer semantics | 先保留 Tomcat semantics |

原則是：**不為了消除 `UnsupportedOperationException` 而製造未驗證的假相容層。**

---

# 第十五章　完整逐流程總圖

## 15.1 Tomcat NIO

```text
Tomcat lifecycle
  ↓
AbstractEndpoint / NioEndpoint
  ↓
bind / accept
  ↓
NioChannel + NioSocketWrapper
  ↓
Poller / Selector
  ↓
readyOps()
  ↓
processKey()
  ├─ READ → processSocket(..., true)
  └─ WRITE → processSocket(..., true)
                 ↓
             executor
                 ↓
         SocketProcessorBase.run()
                 ↓
         ConnectionHandler
                 ↓
         Http11Processor
                 ↓
         CoyoteAdapter / Catalina / Servlet
                 ↓
         transport consumption
                 ↓
         Poller interest update / close
```

## 15.2 NGINX

```text
master / worker lifecycle
  ↓
listening socket
  ↓
ngx_event_process_init()
  ↓
event backend / epoll
  ↓
epoll_wait()
  ↓
ngx_event_t handler
  ├─ read event
  └─ write event
       ↓
connection / buffer / protocol processing
       ↓
ngx_handle_read_event()
ngx_handle_write_event()
       ↓
event registration state
       ↓
next epoll_wait()
```

## 15.3 NativeTomcat 目標

```text
Tomcat lifecycle
  ↓
NativeEndpoint
  ↓
native runtime bind / accept
  ↓
stable handle ↔ NativeSocketWrapper
  ↓
native epoll owner
  ↓
readiness epoch
  ↓
NativeEndpoint
  ↓
real AbstractEndpoint.processSocket()
  ↓
real SocketProcessorBase
  ↓
real Tomcat transport consumption
  ↓
ConnectionHandler / Http11Processor
  ↓
CoyoteAdapter / Catalina / Servlet
  ↓
transport result / desired READ+WRITE interest / close
  ↓
native command queue
  ↓
native event-loop owner
  ↓
exactly one effective final REARM or CLOSE per epoch
  ↓
next native readiness epoch
```

第三張圖是**預想中的目標架構，不是目前完成狀態**。

---

# 第十六章　目前 NativeTomcat 的真正 integration gate

依目前 online repository 最新 source audit，最重要的未完成鏈不是「再寫一點 HTTP code」，而是：

```text
Native readiness
 → real Tomcat SocketWrapperBase
 → real processSocket()
 → real SocketProcessorBase lifecycle
 → actual transport consumption
 → processor completion
 → epoch finalization
 → native stale-command rejection
 → exactly-one effective REARM/CLOSE
```

其中已明確驗證的限制包括：

1. `dispatch=false` 不會建立 Tomcat executor completion boundary；
2. `SocketProcessorBase.run()` 可能在 `doRun()` 前因 closed state 返回；
3. READ/WRITE 必須維持獨立 processor semantics；
4. command queue coalescing 不等於 exactly-one；
5. blocking I/O 的 immediate interest update 是普通 processor epoch 的例外；
6. native event-loop 必須是 epoll registration/close owner；
7. NGINX 可支持 READ/WRITE 分離與 event-owner 分層，但不能提供 Java executor lifecycle 答案。

因此下一階段應優先驗證 **Tomcat executor lifecycle + real `processSocket()` integration + epoch completion envelope**，而不是跳到 Servlet/TCK 或 benchmark。

---

# 附錄 A　已核對的主要 Tomcat 原始碼

基準：Apache Tomcat 11.0.25，commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`。

| 元件 | 原始碼 |
|---|---|
| Endpoint contract | `java/org/apache/tomcat/util/net/AbstractEndpoint.java` |
| Socket wrapper | `java/org/apache/tomcat/util/net/SocketWrapperBase.java` |
| Processor lifecycle | `java/org/apache/tomcat/util/net/SocketProcessorBase.java` |
| NIO endpoint | `java/org/apache/tomcat/util/net/NioEndpoint.java` |
| Protocol handler | `java/org/apache/coyote/AbstractProtocol.java` |
| HTTP/1.1 protocol | `java/org/apache/coyote/http11/AbstractHttp11Protocol.java` |
| HTTP/1.1 processor | `java/org/apache/coyote/http11/Http11Processor.java` |

官方 pinned source：

- https://github.com/apache/tomcat/tree/cbe6e15ee81e2fc6232954292a80cca5d1e84009
- https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/AbstractEndpoint.java
- https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/SocketWrapperBase.java
- https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/SocketProcessorBase.java
- https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java

---

# 附錄 B　已核對的主要 NGINX 原始碼

基準：官方 NGINX 1.30.4，release ref `release-1.30.4`。

| 元件 | 原始碼 | 用途 |
|---|---|---|
| Event core | `src/event/ngx_event.c` | event loop、read/write event handling、timer/process integration |
| Event abstraction | `src/event/ngx_event.h` | event state、event actions、event flags |
| epoll backend | `src/event/modules/ngx_epoll_module.c` | epoll registration、wait、event translation |
| Connection core | `src/core/ngx_connection.c` | connection lifecycle / connection-level native ownership |
| Buffer core | `src/core/ngx_buf.c` 等 | native buffer/chain model |

官方 repository：

- https://github.com/nginx/nginx/tree/release-1.30.4
- https://github.com/nginx/nginx/blob/release-1.30.4/src/event/ngx_event.c
- https://github.com/nginx/nginx/blob/release-1.30.4/src/event/ngx_event.h
- https://github.com/nginx/nginx/blob/release-1.30.4/src/event/modules/ngx_epoll_module.c

NGINX 的作用到此為止：它是 native event architecture cross-check，不是 Servlet/Tomcat contract authority。

---

# 附錄 C　NativeTomcat 的證據分級

本文件使用以下嚴格分級：

- **source-verified**：已由指定 upstream source/body 核驗；
- **implemented**：repository 已有實作；
- **compiled**：正式 build path 已成功編譯；
- **unit-tested**：focused executable test 通過；
- **integration-tested**：真實跨元件 runtime path 已驗證；
- **Servlet/TCK-tested**：Servlet/TCK 行為已驗證；
- **benchmark-verified**：在可重現條件下完成效能測量。

本文件中的「預想」、「目標」、「候選 bridge」、「epoch design」均不得視為 `implemented`；`implemented/source-verified` 也不得自動升級為 `integration-tested`。

---

# 附錄 D　目前已知的文件間歷史差異如何處理

`docs/` 中存在不同階段的稽核文件，因此可能出現「早期文件描述某方法尚未實作」與「較新文件已記錄該方法實作」的時間差。判斷目前狀態時，應以：

1. online `main` 最新 commit；
2. 最新 source/commit diff；
3. 實際 repository file content；
4. 最新 executable verification

為優先順序。

尤其不可把舊文件中的「尚未實作」直接套用到已經被後續 commit 修改的 source；反之，也不可因較新文件宣稱 implemented 就跳過 executable verification。

---

# 附錄 E　新工程對話的最小閱讀順序

1. `docs/work-principles.md`
2. 本文件
3. `docs/native-transport-gate.md`
4. `docs/socket-processor-protocol-audit.md`
5. `docs/native-epoch-lifecycle-bridge-design.md`
6. `docs/native-endpoint-responsibility-matrix.md`
7. `docs/socket-wrapper-upstream-audit.md`
8. pinned Tomcat `AbstractEndpoint.java` / `SocketWrapperBase.java` / `SocketProcessorBase.java` / `NioEndpoint.java`
9. NGINX `ngx_event.c` / `ngx_event.h` / `ngx_epoll_module.c`

任何 implementation change 前，都要重新核對 Tomcat 與 NGINX 原始碼，不得只依賴本文件的二手摘要。
