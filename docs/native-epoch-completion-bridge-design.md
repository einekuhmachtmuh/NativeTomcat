# Native readiness epoch completion bridge design

## Status

本文件是 implementation gate；目前只完成 source-verified design，不代表核心 epoch implementation 已完成。

## Source baseline

- NativeTomcat `main`：以本文件建立時的 online `main` 為準；不得把舊 audit 文件中的 NativeTomcat SHA 當目前 main pin。
- Apache Tomcat 11.0.25：`cbe6e15ee81e2fc6232954292a80cca5d1e84009`。
- `AbstractEndpoint.processSocket()` blob：`85f1c9e23464e17ae14108ae4b04fbf4dad07cdd`。
- `SocketProcessorBase` blob：`25f879d925d8045ee5e3e20cde9ce8543050618b`。
- `NioEndpoint` blob：`21b0cadbbb3ab04351415c0d7c127ab3500ace58`。
- NativeTomcat `NativeEndpoint`：`47f265fb1297b716ee66b17ba1f2026df75d247d`。
- NativeTomcat `NativeSocketWrapper`：`b266a4fd99487592eee30cfa50c6fd272cf1e196`。
- NativeTomcat `NativeTransport`：`8aeaeecaf54119f8da6d83b25913fe1102717b11`。
- NativeTomcat `nt_native_transport.c`：`68d8f87e9d44c2b81c74e9b20d7bb7854d5ceccc`。

## 1. Tomcat completion boundary

Pinned `AbstractEndpoint.processSocket()` 在 `dispatch && executor != null` 時只執行 `executor.execute(sc)`；方法成功返回只表示 submission 成功，不表示 `SocketProcessorBase.run()` 完成。

Pinned `SocketProcessorBase.run()` 是 `final`：先取得 `socketWrapper.getLock()`、lock、檢查 `isClosed()`，然後才進入 subclass `doRun()`，最後 unlock。這表示 completion boundary 應放在 **`sc.run()` 的外層**，不能假設 subclass `doRun()` 的 `finally` 能涵蓋所有 processor lifecycle。

特別是 socket 在 submission 與 worker 啟動之間已被 close 時，`SocketProcessorBase.run()` 會直接 `return`，因此任何只放在 `doRun()` 的 completion callback 都不會執行。

## 2. NativeEndpoint 現況

`NativeEndpoint.processNativeEvent()` 目前以 `processSocket(..., false)` 同步執行 READ/WRITE processor；其 `SocketProcessor.doRun()` 的 `finally` 只在 `doRun()` 被真正進入後執行。

因此目前存在兩個不同問題：

1. ordinary processor 沒有 Tomcat Poller 的 executor submission boundary；
2. 即使把 `dispatch` 改成 `true`，也不能把 `processSocket()` 返回當 completion。

NativeSocketWrapper 的 `requestInterest()` 又直接呼叫 `NativeTransport.rearm()`，所以 completion bridge 必須與 rearm ownership 一起設計，而不能只包裝 Executor。

## 3. Required bridge semantics

第一版 bridge 必須滿足：

```text
native readiness epoch
    ↓
consume ready bits / establish epoch state
    ↓
create SocketProcessorBase
    ↓
Executor submission
    ↓
outer Runnable executes sc.run()
    ↓
completion callback exactly once
    ↓
owner computes final desired interest
    ↓
one effective rearm OR close
```

其中 `completion` 必須包住 **整個 `sc.run()` invocation**，不是只包 `doRun()`。

### Rejection

若 `executor.execute()` 拋出 `RejectedExecutionException`：

- 不得呼叫 normal completion callback；
- 必須進入 failure/close path；
- 該 epoch 不得永久等待；
- 不得因 submission 失敗而重新發布 REARM。

### Closed-before-run

若 worker 啟動後 `SocketProcessorBase.run()` 因 `socketWrapper.isClosed()` 直接 return：

- outer completion callback 仍必須執行；
- completion path 必須看到 CLOSED 狀態；
- 不得重新 REARM。

### Exception / Error

`sc.run()` 本身由 Tomcat `SocketProcessorBase` 不捕捉所有 Throwable；bridge 不得吞掉錯誤。若需要在 completion path 做 close/rearm bookkeeping，必須保留 Throwable 的原有傳播/處理語意，並明確區分 bookkeeping failure 與 processor failure。

## 4. READ + WRITE same readiness

Pinned Nio `Poller.processKey()` 在同一次 selected key 中可以先提交 READ，再提交 WRITE；兩者是兩個 processor work item。

因此 NativeTomcat epoch state 不得使用單一 `processorDone` boolean。至少要有 per-work completion accounting，例如：

```text
pendingWork = number of processor work items actually submitted
completedWork = number of outer run() invocations completed
```

只有所有實際提交的 work 完成，epoch 才能進入 final-interest decision。

如果 READ/WRITE 其中一邊沒有提交 processor（例如對應 blocking waiter 被喚醒），它不應被計入 pending work；它屬於 native readiness consumption / waiter wakeup path。

## 5. Blocking I/O exception

Pinned Tomcat Nio blocking read/write 在 readiness 到達時由 Poller 直接 wake waiter，而不是提交 SocketProcessor。

NativeTomcat 因此可以保留 immediate `requestInterest()` 作為 blocking-wait exception，但必須：

- 明確標示這不是 ordinary processor completion path；
- read/write waiter 狀態與 epoch accounting 分離；
- 不得因 waiter wakeup 再產生一個 ordinary processor completion requirement；
- close 必須能解除 waiter 並終止相關 epoch。

## 6. Rearm ownership

第一版不得讓 ordinary `SocketProcessor` 在 processor execution 中直接完成 kernel rearm。

`NativeSocketWrapper.requestInterest()` 現行 direct `NativeTransport.rearm()` 應視為待拆分的兩層 API：

1. logical desired-interest update；
2. native event-loop owner 的 effective kernel application。

blocking waiter 可以保留受控 immediate registration，但 ordinary processor completion 後的 final mask 必須回到 native owner。

## 7. NGINX cross-check

官方 NGINX epoll backend 的 `ngx_epoll_add_event()` 在加入 READ/WRITE 時會保留另一方向的 active event；`ngx_epoll_del_event()` 也只移除指定方向並保留另一方向。其 `ee.data.ptr` 同時編碼 connection pointer 與 event instance，用來協助 stale event 辨識。這支持 NativeTomcat 必須分離 logical READ/WRITE desired state、kernel applied state 與 event identity。

NGINX 不是 Tomcat completion semantics source，也沒有證明 NativeTomcat 的 `EPOLLONESHOT` 必然應採相同實作；本 cross-check 只用於 native event ownership/lifetime architecture。

## 8. Implementation gate

在修改 `NativeEndpoint` / `NativeSocketWrapper` 前，實作必須先能回答並測試：

1. epoch identity 在哪裡建立；
2. native event arrival 後 ready bits 何時被視為 consumed；
3. READ/WRITE 各自產生多少 work item；
4. submission rejection 如何終止 epoch；
5. closed-before-run 如何完成 epoch；
6. `sc.run()` exception 如何完成 bookkeeping；
7. blocking waiter 如何與 ordinary processor 分離；
8. CLOSE 如何壓過 REARM；
9. final desired mask 如何只被 native owner effective-apply 一次；
10. stale handle / stale epoch 如何被拒絕；
11. effective `epoll_ctl()` 次數如何在 native test 中直接計數驗證。

## Decision

本輪不直接把 `NativeEndpoint.processNativeEvent()` 改成 `dispatch=true`，也不直接刪除 `NativeTransport.rearm()`。先建立並單獨測試 completion/lifecycle bridge，確認它能涵蓋 `SocketProcessorBase.run()` 的 closed-before-doRun 路徑，再將 bridge 接入 native epoch owner。

## Verification level

- Tomcat source contract：`source-verified`
- NativeTomcat current implementation：`source-verified`
- NGINX epoll architecture：`source-verified`
- Completion bridge implementation：`not implemented`
- End-to-end/runtime：`blocked / not yet reached`
