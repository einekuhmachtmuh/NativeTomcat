# NativeTomcat 工作基本原則

本文件是 NativeTomcat 的**強制工作守則**。它規範工作方法，不是固定 roadmap；每次開始新步驟都必須重新核驗現況。

## 1. 核心原則

1. 使用繁體中文與臺灣電腦科學慣用術語。
2. 新增/修改的 C、Java、Ant 原始碼以 Tab 縮排；未修改的上游原始碼保留原格式。
3. 重要結論必須能回溯到可驗證的原始碼、規範或可靠技術文件；無法確認時標示「未確認／blocked」，不得猜測。
4. 嚴格區分：**規範要求、上游實作、NativeTomcat 設計、理論分析、實測結果**。
5. 不得把 C、event loop、native memory、JNI/FFM、較少 GC 等本身當成效能證據；效能主張必須有成本模型與 benchmark。
6. 不得虛構檔案、類別、函式、API、規範要求、測試或 benchmark；compile/surface test PASS 不等於 runtime semantics 正確。

## 2. 固定基準與來源

- **Servlet**：Jakarta Servlet 6.1；Compatibility Mode 必須維持外部可觀察語意。
- **Tomcat**：Apache Tomcat 11.0.25，pinned commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`。不得以未核驗的 Tomcat `main` 取代此基準。
- **NGINX**：以開始工作時核實的官方穩定版/source 為 native event architecture 參考；不得把 NGINX implementation detail 當成 Servlet/Tomcat requirement。
- **HotSpot**：以實際支援的 OpenJDK 正式版本及固定 tag/commit 為準；不得用舊版 HotSpot 推論新版。
- 來源優先順序：官方 Git repository → official raw source → official release → official specification；核心結論不得依賴第三方 fork。
- 每次進入新架構問題前，先記錄版本、commit/tag、repository、source root、相關檔案/函式及版本差異。

## 3. 每一步的強制核驗流程

依序執行，不能跳過必要 gate：

### A. 實際 repo

先檢查目前 code、tests、build/test path，並分清「source 存在」與「已運作／已測試」。

若同時有本機 repo 與 GitHub：
1. 確認本機有 `.git`、branch、HEAD、status；沒有就只能做 snapshot content comparison。
2. 記錄本機 HEAD 與線上 target ref SHA；若沒有共同祖先，說明限制。
3. 優先做完整 Git commit/ref diff，不只比已知修改檔。
4. 將差異分類為 local-unsynced、online-only、untracked/uncommitted、format/line-ending、uncomparable。
5. 每個重要 code/docs/test 修改後，先確認線上 commit，再重新取得 ref 並比較；不得使用 stale snapshot。
6. 差異未解釋前，一律使用 `not synchronized / diff incomplete / verification blocked`，不得宣稱同步。

### B. Docs

讀取所有相關 docs，而非只讀預計修改的檔案；若 docs 互相衝突，回到 source verification，不以文件新舊或 roadmap 裁決。每項狀態明確標示 `implemented / source-verified / tested / deferred / blocked`。

### C. Tomcat pinned source

凡 implementation materially 涉及 Tomcat Java source，必須核對**實際 method body、call chain、ownership/lifetime、locking、executor/thread model、event mapping、buffer/read/write、error/close/recycle 與 abstract/subclass contract**。

並遵守 `docs/tomcat-source-migration-rule.md`：
- repo `java/` 已有 exact package/path source → 使用並核對；
- 沒有 → 先遷入 pinned exact source，不得憑記憶/API 文件手寫替代；
- exact source 無法取得或核驗 → `source verification blocked/incomplete`，不得宣稱 migration/equivalence 完成；
- supporting Tomcat classes 同樣遞迴處理。

### D. NGINX source

凡涉及 native event loop、read/write readiness、registration/rearm、connection lifecycle、event/handler separation，都要直接對照 NGINX source。至少核對 `src/event/ngx_event.c`、`src/event/ngx_event.h`、epoll backend，必要時追到 connection/buffer/core/http。

固定分層：

`kernel readiness → native event handling → transport consumption → Java/Tomcat processing → Servlet readiness`

任兩層都不得視為同一語意。

### E. 重新對齊

source verification 後才決定下一步，並同步修正 code、docs、steps。若新證據推翻舊 roadmap，修改 roadmap，不硬接續。

## 4. Servlet 與 C 化政策

### Servlet 6.1

Compatibility Mode 必須保留 API、lifecycle、dispatch、threading、Request/Response、exception/error、async、Filter/Listener、Session、security 與必要 HTTP semantics。若提出 Native Extension，必須明確標示為非規範擴充且不得破壞 Compatibility Mode。

### C 化決策

每個候選元件至少檢查：JNI/FFM crossing、hot-path 頻率、allocation/GC、copy/bandwidth、syscall、lock/atomic/context switch、event-driven I/O、HotSpot/JIT 可最佳化程度、複雜度、thread-safety、ownership/lifetime、錯誤處理、安全、維護與可 benchmark 性。

- JIT 已能有效最佳化 → 不因「是 Java」而 C 化。
- 若 C 化增加 crossing、copy、同步或 lifecycle 複雜度 → 預設不 C 化。
- 優先考慮 socket/event loop、readiness、buffer、syscall-intensive path。
- Servlet API、application lifecycle、class loading、Filter/Listener/Session、dispatch、JSP/Jasper、application object model → Java 優先。
- 每項修改標記：`performance / boundary / integration / compatibility-required / do-not-modify`。

## 5. 固定 integration contract

Native readiness 接入 Tomcat **只能**依下列順序建立與驗證：

```text
Native event
  ↓
stable native-handle ↔ Java transport object
  ↓
real SocketWrapperBase
  ↓
AbstractEndpoint.processSocket()
  ↓
SocketProcessorBase
  ↓
transport consumption
  ↓
native event-loop owner determines next interest
  ↓
exactly one rearm / close
```

這是 gate 順序，不代表全部已完成。上一層的 source、ownership/lifetime、build/test 條件未成立，不得啟用下一層。

強制語意：
- stable handle 必須有可驗證的 registry owner 與 lifetime；不得靠暫時 Java map/callback lifetime。
- `SocketWrapperBase` 必須是 pinned Tomcat exact source；compatibility shell 不算等價。
- `processSocket()` / `SocketProcessorBase` 必須遵守 pinned Tomcat 的 dispatch、executor、locking、lifecycle contract；surface test 不能取代 integration。
- Java task enqueue 不等於 transport consumption。
- Java worker 不得直接操作 event-loop-owned epoll registration；interest/rearm/close 的最終決定權屬 native event-loop owner。
- 每個 event-processing cycle 必須由 native owner **恰好一次** rearm 或 close；不得因 enqueue 提前 rearm，也不得雙重執行。
- `kernel readiness` ≠ Servlet `isReady()`；`event dispatched` ≠ `event consumed`；`Java task submitted` ≠ `SocketProcessor processed`。

## 6. Native / Java / ownership / thread 原則

預設架構：**C network/runtime + Java Servlet execution**，不是把 Tomcat 翻成 C。

- C：socket、accept、event loop、readiness、connection state、I/O buffer、適合 native 化的 parsing、TLS integration、back-pressure、native resource lifecycle。
- Java：Servlet API、application lifecycle、dispatch、Filter/Listener/Session、ClassLoader、JSP/Jasper、application-facing semantics。
- C+Java：Request/Response bridge、async I/O、buffer ownership、socket state、metadata、error propagation、connection lifecycle。
- 避免每個 getter/API method 都 crossing；優先批次、明確邊界、direct/borrowed buffer、固定 lifetime。
- 每個 C/Java boundary data structure 必須定義 representation、ownership、mutability、lifetime、thread affinity、alignment、ABI、error semantics。
- 每個 native handle 必須有明確 create/transfer/borrow/return/destroy 規則；不得以「應該不會發生」合理化 NULL、overflow、UAF、double-free、race。
- Thread model 不預設單 event loop 或一連線一執行緒；需要時比較 event-loop/worker、connection/request affinity、blocking/async、virtual/platform threads、lock strategy，依 throughput、tail latency、CPU、contention、context switch、cache、memory、correctness 驗證。

## 7. Request/Response 與實作邊界

實際資料流必須以 source 對照：

`Client → TCP/TLS → socket → accept → event loop → connection state → read buffer → HTTP parser → URI/header/body → Request metadata → mapping → Context/Wrapper → Filter chain → Servlet.service() → ServletInput/OutputStream → response buffer → HTTP encoding/chunking → TLS → socket write → keep-alive/close`

每一階段追蹤 owner、memory、allocation、lifetime、thread、sync、copy/no-copy、error、cancel、back-pressure、cleanup。

不得在真實 `SocketWrapper → processSocket → SocketProcessor → ProtocolHandler` 建立前修改/跳到 `Http11Processor` 或 Servlet integration。

## 8. Build / test / 安全

- **Ant 是唯一正式 build/test orchestrator**；沿用 pinned Tomcat `build.xml`/`BUILDING.txt` 架構，不以 Maven/Gradle/CMake/Meson 取代。
- Native compile/link、Java compile、tests、Servlet tests、benchmark 都必須由 Ant 可驅動。
- 測試前檢查 OS/arch、JDK/JAVA_HOME、Ant、compiler/linker、JNI headers、OpenSSL、system libs、library path、port、dependencies、permissions；環境未完成直接 fail，不進 benchmark。
- 明確區分 Tomcat tests、NativeTomcat tests、Servlet TCK、benchmark；benchmark 不取代 compatibility tests。
- 安全至少覆蓋 parsing、URI/header/request validation、size/timeout/keep-alive/slowloris、memory/integer safety、race/UAF/double-free、TLS/cert/ALPN/HTTP2、request smuggling、path traversal、malformed input、resource exhaustion、active-request shutdown、async cancellation、cleanup，並回到 Tomcat/HTTP/TLS source/spec 驗證。

## 9. 不得跳過的驗證層級

每項工作標示最高已達層級：

1. **source verified**
2. **code implemented**
3. **compilation verified**
4. **unit test verified**
5. **integration verified**
6. **Servlet/TCK verified**
7. **benchmark verified**

可另標 `specification-verified`、`theoretical analysis`，但不能取代 runtime verification。不得把 unit/surface/compile 結果升級成 integration/TCK/benchmark。

Benchmark 至少記錄 latency (平均/P50/P95/P99/P99.9)、throughput、connections、keep-alive、RSS/heap/native memory、allocation、GC、syscall、context switch、lock contention、CPU、JNI/FFM crossing、bytes copied、TLS cost、error rate、overload tail latency；與 Tomcat（必要時 NGINX）比較時保持相同硬體、OS、JDK、compiler、workload、keep-alive、payload、TLS、concurrency、warm-up、iteration、統計方法。若 C 化較慢，保留結果並修改架構，不扭曲結論。

## 10. 工作記錄與輸出

每完成重要步驟記錄：
- 修改檔案；
- Tomcat/NGINX/Servlet source、版本、commit/spec；
- 採用的 contract；
- code/docs/steps 修正；
- compile/test/verification level；
- 本機與線上 diff 結果；
- remaining / blocked / deferred。

正式大階段報告可依：**基準 → source mapping → Servlet constraints → architecture/ownership → request/response flow → C/Java boundary → build/test → implementation → tests/TCK/benchmark → final review**。

## 11. 第一階段與 roadmap gate

第一階段的目標是建立可驗證的最小 end-to-end path，而非一次重寫 Tomcat。後續優先順序由 source/contract/test 決定；目前固定 integration gate 不得跳過。

第二階段才處理：Async、non-blocking I/O、TLS、HTTP/2、session、JSP/Jasper、advanced buffering、zero-copy、back-pressure、connection scaling 等尚未由當前 gate 證明必要的複雜功能。

**最終目標**：C 負責確有成本優勢的 network/I/O/runtime path，Java/HotSpot 保留 Servlet/application semantics，以可重現測試證明相容性與效能；不是「用 C 重寫 Tomcat」。
