# Native interest-mask implementation audit

## Scope

本步只處理前一輪 source audit 發現的 correctness issue：NativeTomcat 原本以 `boolean want_write` 表示 transport interest，無法保持 Tomcat NIO 的獨立 READ/WRITE interest set。

本步**沒有**宣稱 exactly-one readiness-cycle completion 已完成，也沒有開始 HTTP/Servlet integration。

## Source baseline

- NativeTomcat 工作開始時 HEAD：`d8ef42deceb00f645b4e5e979ccb113e820f0322`。
- Apache Tomcat 11.0.25：`cbe6e15ee81e2fc6232954292a80cca5d1e84009`。
- NGINX：官方 `master` event/epoll source；NGINX 1.30.4 exact source 本輪仍未取得可靠版本證據，因此不把它當版本基準。

## Changes actually made

1. `native/include/nt_runtime.h`
   - 新增 `NT_RUNTIME_INTEREST_READ` / `NT_RUNTIME_INTEREST_WRITE`。
   - `nt_runtime_request_rearm()` 與 `nt_runtime_rearm_connection()` 改為接受完整 `unsigned interest_mask`。

2. `java/org/apache/tomcat/util/net/NativeTransport.java`
   - `rearm(long, boolean)` 改為 `rearm(long, int)`。

3. `native/src/nt_native_transport.c`
   - JNI rearm bridge 改為傳遞完整 interest mask。

4. `native/src/nt_runtime.c`
   - command 改保存完整 interest mask。
   - command coalescing 改為保留最後完整 desired mask；CLOSE 仍優先。
   - `epoll_ctl(EPOLL_CTL_MOD)` 只依 READ/WRITE bits 建立 EPOLLIN/EPOLLOUT，EPOLLRDHUP 與 EPOLLONESHOT 保留。
   - `interest_mask == 0` 合法，代表本 epoch 暫無 READ/WRITE readiness interest。

5. `java/org/apache/tomcat/util/net/NativeSocketWrapper.java`
   - 新增獨立 READ/WRITE desired-interest state。
   - `registerReadInterest()` 使用 OR semantics，不清除 WRITE。
   - `registerWriteInterest()` 使用 OR semantics，不清除 READ。
   - blocking read/write 的 immediate rearm 也改成以完整 desired mask 發送。
   - native readiness 到達時，先消費對應 readiness bits：READ 只清 READ、WRITE 只清 WRITE；ERROR 清空兩者。
   - 修正 close 與 interest/read/write lock 的取得順序，避免 `readLock/writeLock -> interestLock` 與 `interestLock -> readLock/writeLock` 形成循環等待。

## Tomcat cross-check

Pinned Tomcat `NioEndpoint.Poller.processKey()` 先以 `unreg()` 移除已回報的 ready operations，再獨立處理 READ、WRITE；`events()` 對新的 interest 使用 `key.interestOps() | interestOps`。因此本步的 mask representation 與 Tomcat 的 source-derived semantics 一致。

這只修正 interest representation，不等同完整 `NioSocketWrapper`、Poller 或 SocketProcessor lifecycle equivalence。

## NGINX cross-check

官方 NGINX epoll backend 的 `ngx_epoll_add_event()` 對 READ/WRITE 分別處理，並在 active opposite-direction event 存在時以 `events |= prev` 保留另一方向；`ngx_epoll_del_event()` 亦保留仍 active 的 opposite direction。這獨立支持「READ/WRITE 必須分開表示」的 native event architecture 判斷。

NGINX 使用的 clear/edge-triggered event model 不被當作 NativeTomcat `EPOLLONESHOT` 的直接實作依據。

## Verification status

### Source-verified

- 所有上述修改均已重新從 GitHub `main` 取得並核對最新 blob。
- 最新 HEAD：`cea16502651ffabb8c3217f6194a8d740382d475`。
- 最新 HEAD 的工作序列包含 interest-mask API、native mask application、Java wrapper mask state，以及 close lock-order 修正。

### Compile / test

本輪無本機 checkout；直接 `git clone` 嘗試因目前執行環境無法解析 `github.com` 而失敗。因此沒有把 compile/test 結果猜成 PASS。

狀態：`compiled = blocked`、`unit-tested = blocked`。

既有 `build_test.xml` 仍是正式 Ant verification path，但本輪沒有可執行該 Ant build 的本機 source tree / dependency environment。

### Remaining correctness gates

1. **Exactly-one completion 尚未完成。** 目前 `register*Interest()` 仍可直接產生 native rearm command；command coalescing 仍不足以證明一個 readiness epoch 恰好一次 effective rearm/close。
2. **Applied-state deduplication 尚未完成。** Native owner 尚未保存足夠的 applied-interest state 來可靠判斷 `epoll_ctl()` 是否為 no-op。
3. **Native lifetime race 尚未完成證明。** `nt_runtime_find_connection()` 解除 `connection_mutex` 後，Java I/O 與 native close 的物件 lifetime 仍需獨立核驗。
4. **RDHUP / last-data ordering 尚未完成 proof。** 目前 NativeEndpoint 對 ERROR/RDHUP/READ 的優先順序仍需在真實 socket integration 驗證。
5. **HTTP/1.1 integration 尚未開始。** 不得宣稱 Servlet/TCK/benchmark 相容。

## Next minimal verifiable step

下一步只處理第 1、2 項：建立明確的 readiness-epoch completion boundary，使 ordinary SocketProcessor path 在不等待 native event loop 的前提下，最後只發布一個完整 desired mask 或 CLOSE；同時由 native owner 維護 applied-interest state，將 command coalescing 與 effective `epoll_ctl()` deduplication 分開驗證。
