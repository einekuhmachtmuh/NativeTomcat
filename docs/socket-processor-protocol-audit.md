# SocketProcessor 與 HTTP/1.1 protocol gate 稽核

## 範圍與證據等級

- **[SOURCE VERIFIED]**：於 2026-09-07 由官方 Apache Tomcat repository checkout `cbe6e15ee81e2fc6232954292a80cca5d1e84009`（11.0.25）逐一比較。
- **[SOURCE VERIFIED]**：`AbstractEndpoint.java`、`SocketWrapperBase.java`、`SocketProcessorBase.java`、`NioEndpoint.java`、`AbstractProtocol.java`、`AbstractHttp11Protocol.java`、`Http11Processor.java` 在本 repository `java/` 的 blob SHA-1 與上述 pinned checkout 相同；它們不是以其他版本或 API 文件替代。
- **[COMPILED]**：JDK 21、Jakarta Servlet API 6.1.0 與真實 bnd jar 下，`build.xml` 的 `java-compile` 已通過。
- **[UNIT TEST PASS]**：`build_test.xml` 的既有 dispatcher、socket-wrapper lock surface、native runtime 及 JVM dispatch tests 已通過。
- **未確認 / blocked**：這些編譯與元件測試沒有建立一個真實 HTTP 連線，也沒有證明 HTTP request/response 完成後恰好一次 native rearm 或 close。因此本文件不宣稱 integration、Servlet TCK 或 benchmark 通過。

## Tomcat dispatch contract

### `SocketProcessorBase.run()`

Pinned `SocketProcessorBase.run()` 取得 `socketWrapper.getLock()`，在同一 wrapper 的互斥鎖內檢查 closed 狀態後才呼叫 `doRun()`，最後必定解鎖。這是同一連線的 READ 與 WRITE processor 不可平行執行的 contract；`NativeEndpoint.SocketProcessor` 經由此基底類別取得此鎖定，不得繞過它。

### `AbstractEndpoint.processSocket()`

Pinned `AbstractEndpoint.processSocket()` 依序：從 `processorCache` 取用 processor，或建立 processor；對重用的 processor 呼叫 `reset()`；若 `dispatch` 且 executor 存在則 `executor.execute()`，否則同步 `run()`；並把 rejection/throwable 回報為 `false`。本地 `NativeEndpoint.processNativeEvent()` 使用這個既有方法，不能自行模擬 cache 或 executor 行為。

### `NioEndpoint.Poller.processKey()` 的對照

Pinned NIO poller 先取消 ready interest，再按 READ、WRITE 順序處理同一個 selection key。每個方向先處理 pending operation、再喚醒對應 blocking waiter，最後才呼叫 `processSocket(..., OPEN_READ/OPEN_WRITE, true)`；任何 dispatch failure 導致 close。這確認 NativeEndpoint 必須保留同批 READ 與 WRITE，且 waiter 不能另行收到同方向 processor。

NativeEndpoint 已保留 READ 後的 WRITE dispatch，但目前以 `dispatch=false` 呼叫 `processSocket()`。這是 NativeEventDispatcher 已完成 executor hand-off 的設計選擇，不是 NIO poller 的逐字等價；其 executor/thread ownership 仍須在真實整合測試驗證。

## `AbstractProtocol.ConnectionHandler` contract

Pinned handler 對 wrapper 取走 current processor，取消 waiting processor 註冊，依序取得 negotiated-protocol processor、recycled processor 或 `createProcessor()` 建立者，接著把 SSL support 綁定到 processor 並呼叫 `processor.process(wrapper, status)`。它依 `SocketState` 處理 upgrade、async、long-running、open 與 closed，最後把仍存活的 processor 關聯回 wrapper，或 recycle/release。

因此 NativeEndpoint.SocketProcessor 僅可依 handler 回傳 `CLOSED` 關閉 wrapper；它不能自行把 `OPEN`、`LONG`、async、upgrade 或 processor recycle 簡化成 close。這些狀態目前由 pinned `AbstractProtocol.ConnectionHandler` 保有，但尚未透過 NativeEndpoint 建立並執行完整 protocol handler 的 integration proof。

## `AbstractHttp11Protocol<Long>` 與 `Http11Processor`

`AbstractHttp11Protocol<S>` 的型別參數可接受 `Long`；其 processor factory 建立 `Http11Processor`，而 pinned `Http11Processor.setSocketWrapper(SocketWrapperBase<?>)` 初始化 `Http11InputBuffer` 與 `Http11OutputBuffer`。此處沒有將 socket 型別轉型成 `NioChannel` 或 `NioSocketWrapper` 的直接依賴。

這只證明 HTTP/1.1 processor 的 wrapper 型別入口是 generic，**不**證明 transport 可用。`Http11Processor` 仍會透過 wrapper 讀取、寫入、flush、interest registration、sendfile、SSL support、upgrade 與 application buffer contract。NativeSocketWrapper 目前明確對 sendfile、TLS 與 vectored async I/O 丟出 `UnsupportedOperationException`；因此 native HTTP/1.1 path 在這些分支不可宣稱相容。

## native rearm / close 的未完成證明

目標 cycle 仍為：

```text
native readiness -> NativeSocketWrapper -> processSocket -> SocketProcessorBase
    -> ConnectionHandler / Http11Processor transport consumption
    -> native command queue -> one rearm or close
```

目前 `NativeSocketWrapper` 在 blocking read/write 的 WOULD_BLOCK 情況及 `registerReadInterest()`/`registerWriteInterest()` 時提交 native rearm，`doClose()` 提交 native close；native event-loop 是 `epoll_ctl()` owner。然而 `NativeEndpoint.SocketProcessor.doRun()` 在 handler 回傳 `OPEN`、`LONG`、async 或 keep-alive 時沒有一個已驗證的、明確的 completion-to-rearm decision。不得把 command queue 的 coalescing 當成 HTTP event cycle 已經「exactly one rearm/close」的證據。

下一步必須先以真實 NativeHttp11Protocol/ConnectionHandler、loopback socket 與 native runtime 建立 integration test，量測每個完成的 processor cycle 所提交及實際執行的 rearm/close；在該測試前，不得進入 Servlet/Catalina/TCK 或效能宣稱。

## NGINX 1.30.4 交叉核對

官方 NGINX `src/event/ngx_event.c` 的 `ngx_handle_read_event()` 與 `ngx_handle_write_event()` 將 readiness handler 與 event registration 分開；`src/event/modules/ngx_epoll_module.c` 使用 read `EPOLLIN | EPOLLRDHUP`、write `EPOLLOUT`，並以 `EPOLLET` 作為目前 clear-event backend 模式。這支持 NativeTomcat 將 readiness、transport consumption 與 interest management 分層，但不定義 Tomcat/Servlet 語意，亦不表示 NGINX 以 `EPOLLONESHOT` 實作 NativeTomcat 的 ownership model。
