# Native readiness epoch source audit

## Purpose

本輪只驗證 exactly-one readiness epoch 所需的 source contract；不在尚未證明 lifecycle/dispatch boundary 前直接修改核心 I/O code。

## Pinned sources

- NativeTomcat main：`5ca5dadd4a0cacbb85247b7aa246836ccb2f29aa`
- Apache Tomcat 11.0.25：`cbe6e15ee81e2fc6232954292a80cca5d1e84009`
- NGINX：官方 `master` 的 `src/event/modules/ngx_epoll_module.c`；本輪仍不把未取得可靠版本證據的 NGINX 1.30.4 當版本基準。

## Tomcat source findings

### 1. Readiness consumption 與 interest update 是兩個步驟

Pinned `NioEndpoint.Poller.processKey()` 在處理 readable/writable 前先呼叫 `unreg(sk, socketWrapper, sk.readyOps())`。`unreg()` 透過 `sk.interestOps() & (~readyOps)` 移除本次 ready operations；之後 READ、WRITE 分開處理。

這確認 NativeTomcat 必須先消費本次 readiness，再計算下一個 desired interest；不能把「事件已送出」視為「下一個 interest 已決定」。

### 2. READ/WRITE 可以在同一次 kernel readiness 中各自進入 `processSocket()`

Pinned `processKey()` 明確先處理 READ，再在沒有 close 的條件下處理 WRITE；兩者各自呼叫 `processSocket(..., true)`。

因此 NativeTomcat 的一個 native readiness notification 可能產生兩個真正的 `SocketProcessorBase` work item。

### 3. `SocketProcessorBase.run()` 才是可靠的 processing completion boundary

Pinned `SocketProcessorBase.run()` 先取得 `socketWrapper.getLock()`，再檢查 `socketWrapper.isClosed()`，然後執行 `doRun()`，最後釋放 lock。

因此：

- `processSocket()` 成功返回只證明 work 已提交；
- Executor task 啟動不等於 processing 完成；
- `doRun()` 返回前不能宣稱該 processor 已完成；
- socket close 可能使後續 processor 在 `run()` 的 closed check 直接 return，而不進入 `doRun()`。

這最後一點是設計 epoch completion callback 時必須明確處理的情況。

### 4. Tomcat 的 `registerReadInterest()` / `registerWriteInterest()` 是 Poller event enqueue

Pinned `NioSocketWrapper.registerReadInterest()` 與 `registerWriteInterest()` 分別向 Poller 加入 `OP_READ` / `OP_WRITE`；Poller `events()` 以 `key.interestOps() | interestOps` 合併新的 interest。

因此 NativeTomcat 的 independent READ/WRITE desired-state 修正是必要的；但 NativeTomcat 使用 `EPOLLONESHOT`，所以「logical desired interest」與「kernel applied interest」仍需分層管理。

### 5. Blocking I/O 是明確例外

Pinned `NioSocketWrapper.fillReadBuffer()` / `doWrite()` 在 blocking I/O 遇到 `0` 時，先設定 `readBlocking` / `writeBlocking`，再呼叫對應 `register*Interest()` 並等待。這表示 NativeTomcat 的 epoch design 不能簡單禁止所有 processor-thread rearm；blocking waiter 的 immediate wakeup/rearm 必須作為獨立例外建模。

## NGINX cross-check

官方 NGINX epoll backend 對 READ 與 WRITE 維護獨立 event object。`ngx_epoll_add_event()` 在加入一個方向時，若另一方向 active，會以 `events |= prev` 保留另一方向；`ngx_epoll_del_event()` 也只移除指定方向並保留另一方向。

`ngx_epoll_process_events()` 對 EPOLLIN 與 EPOLLOUT 分別檢查 active state 並分別 dispatch；對 EPOLLERR/EPOLLHUP 則先補入 EPOLLIN|EPOLLOUT。

這支持 NativeTomcat 的分層判斷：kernel readiness、native event handling、transport consumption、上層 processing 不應混成一個 boolean state。

NGINX 的 event backend 是 architecture reference；它不是 NativeTomcat `EPOLLONESHOT` 的直接 semantics source，也不能用來宣稱 exactly-one epoch 已被 NGINX 證明。

## NativeTomcat current gap

目前 `NativeSocketWrapper.requestInterest()` 在更新 `interestOps` 後直接呼叫 `NativeTransport.rearm()`。因此即使 READ/WRITE mask representation 已正確，ordinary SocketProcessor 仍可各自發布 rearm command。

`NativeEventDispatcher` 的 per-connection OR coalescing 只保證 pending Java notification 的合併與單一 runnable ownership；它沒有等待 `SocketProcessorBase.run()` 完成。因此不能把 dispatcher return 或 executor submission 當作 readiness epoch completion。

native command queue 的 coalescing 也只是在 command application 前合併同一 handle 的 command；它不等於 exactly-one effective `epoll_ctl(EPOLL_CTL_MOD)`。

## Design gate before implementation

下一個 implementation 必須同時滿足：

1. native readiness arrival 建立明確 epoch identity；
2. 本次 READ/WRITE readiness 先被消費；
3. 每個實際提交的 `SocketProcessorBase` 都有可驗證 completion path；
4. processor 因 socket 已 closed 而在 `SocketProcessorBase.run()` 中提前 return 時，epoch 不會永久等待；
5. CLOSE 可以取消/終結該 epoch，且不得在 CLOSE 後重新 REARM；
6. ordinary processor path 不直接發布 kernel rearm；
7. blocking I/O 的 immediate interest registration 明確標示為例外，且不會與 ordinary epoch 形成未定義的 overlapping epoch；
8. native event-loop owner 最終只執行一個有效的 final desired-mask application 或 CLOSE；
9. native owner 保存 applied-interest state，將 command coalescing 與 effective `epoll_ctl()` deduplication 分開驗證；
10. stale handle/close/lifetime boundary 在 epoch completion 前後都可證明安全。

## Decision

本輪 **不修改 NativeEndpoint / NativeSocketWrapper 的 epoch 核心實作**。原因不是缺乏實作意願，而是 pinned Tomcat source 已證明目前的 `processSocket()` → Executor → final `SocketProcessorBase.run()` boundary，單靠目前的 `NativeEndpoint.SocketProcessor.doRun()` 無法涵蓋 closed-before-doRun 與 blocking-immediate-rearm 兩個必要情況。

下一個最小實作步驟應先設計並 source-verify 一個不破壞 Tomcat `processSocket()` / `SocketProcessorBase` contract 的 completion/lifecycle bridge，再進行 code change。

## Verification level

- Tomcat source contract：`source-verified`
- NGINX event architecture cross-check：`source-verified`
- NativeTomcat epoch implementation：`not implemented`
- Compile/runtime/integration：`blocked`（目前本機 checkout/網路環境仍無法完成本機 build）
