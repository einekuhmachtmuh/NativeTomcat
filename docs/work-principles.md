# NativeTomcat 工作基本原則

本文件是 NativeTomcat 的**現行強制工作守則**；它規範工作方法與驗證門檻，不是不可修改的永久 roadmap。若新 source、測試或實測證據推翻現有判斷，應先更新本文件/roadmap，再繼續工作。

## 1. 不可違反

1. 使用繁體中文與臺灣電腦科學慣用術語。
2. 新增/修改的 C、Java、Ant 原始碼使用 Tab；未修改的 upstream source 保留原格式。
3. 重要結論必須可回溯至 source、spec 或可靠技術文件；無法確認即標示 `未確認 / blocked`，不得猜測。
4. 嚴格區分 `spec requirement`、`upstream implementation`、`NativeTomcat design`、`theoretical analysis`、`measured result`。
5. 不得把 C、event loop、native memory、JNI/FFM、較少 GC 等本身當成效能證據；效能主張必須有成本模型與 benchmark。
6. 不得虛構檔案、類別、函式、API、測試或 benchmark；`compile/surface test PASS` 不等於 runtime semantics 正確。
7. **若 GitHub/外部網路受阻，先確認阻斷；有可靠的官方替代通道時直接繞過繼續工作；只有必要 source 無法取得或核驗時才標示 `blocked`。**

## 2. 固定基準與 source-first

- Servlet：Jakarta Servlet 6.1；Compatibility Mode 維持外部可觀察語意。
- Tomcat：11.0.25，pinned `cbe6e15ee81e2fc6232954292a80cca5d1e84009`；不得以未核驗的 Tomcat `main` 取代。
- NGINX：工作開始時核實的官方 source/release，只作 native event architecture reference，不是 Servlet/Tomcat requirement。
- HotSpot：以實際支援的 OpenJDK 正式版本及固定 tag/commit 為準。
- Source 優先序：官方 Git repository → official raw source → official release → official specification；核心結論不得依賴第三方 fork。
- 凡 implementation/planning/debugging/verification materially 涉及 Tomcat Java implementation class：先檢查 repo `java/` 原 package/path；已有則核對，沒有則先取得 pinned exact source，禁止憑記憶/API 文件重建；supporting classes 遞迴遵守。
- Exact source 無法取得/核驗 → `source-verification incomplete/blocked`，不得宣稱 migration/equivalence 完成。
- Java source migration 後，`build.xml`、`build_test.xml` 都必須有明確 compile/verification path。
- Compatibility shell 只能是明確標示的暫時 checkpoint；不能因 method name、compile 或 surface test 相同而視為 upstream equivalent。

## 3. 每一步核驗流程

**A. Repo / docs**
- 先讀相關 code、tests、build path、直接相關 docs；區分 `source exists`、`implemented`、`tested`。
- 有本機 Git 才做 Git diff；記錄 local HEAD/status 與 online target SHA。沒有 `.git` 只能做 snapshot content comparison。
- 優先完整 commit/ref diff；差異分類為 `local-unsynced`、`online-only`、`untracked/uncommitted`、`format/line-ending`、`uncomparable`。
- 每個重要修改後重新取得 ref/commit 核對；差異未解釋前標示 `not synchronized / diff incomplete / verification blocked`。

**B. Tomcat**
- 核對實際 method body、call chain、ownership/lifetime、locking、executor/thread model、event mapping、buffer/read/write、error/close/recycle 與 abstract/subclass contract。
- `build_test.xml` 不是 upstream Tomcat build 的替代品；upstream build 仍是 Tomcat distribution authority。

**C. NGINX**
- 涉及 event loop、read/write readiness、registration/rearm、connection lifecycle、event/handler separation 時，直接核對官方 `src/event/ngx_event.c`、`ngx_event.h`、epoll backend，必要時追 connection/buffer/core/http。
- 固定分層：`kernel readiness → native event handling → transport consumption → Java/Tomcat processing → Servlet readiness`；不同層不得視為同一語意。

**D. Re-align**
- Source verification 後才決定下一步；新證據推翻 roadmap 時，修改 roadmap，不硬接續；每一步同步更新 code、docs、tests/steps。

## 4. DNS 受阻時的增量本機 mirror

- 若確認 shell 的 Git/HTTP(S) 連線因 DNS、egress、proxy 或其他環境限制無法直接取得 repository，**不得反覆以 `git clone`、整 repo `curl` 等一次性方式繞過**；先記錄阻斷證據。
- 若有可靠的官方替代通道（例如 GitHub connector），以該通道取得**目前 online target ref/commit**，再按檔案或必要時按小段內容增量轉移到 `/tmp/NativeTomcat-online/` 等本機 mirror；每個檔案記錄 online path、ref/commit、blob SHA，分段轉移時另記錄範圍與驗證結果。
- 增量 mirror 必須保留本機 `.git`；每批轉移後先做 content/SHA 驗證，再 commit。不得把舊 archive、prototype 或不同版本 source 混入目前 target。
- mirror 尚未完整時明確標示 `partial mirror`；只可對已轉移且驗證的 source 執行本機 build/test。不得把 partial mirror 當完整 checkout。
- **DNS/外部網路受阻後，增量 mirror 成為當前工作的主要本機 repo**：後續 source audit、編輯、build、test、diff 與工作記錄優先以此 mirror 執行；online official channel 僅作 source/reference、同步與 SHA/diff 驗證，不因 online 可讀而假裝本機已完整同步。
- 若工作需要尚未轉入 mirror 的檔案，先從官方替代通道增量取得並驗證，再繼續；若必要 source 無法取得，標示 `source-verification incomplete/blocked`。
- mirror 與 online target 的差異仍依本文件第 3 節分類；任何重要修改後重新取得 online ref/commit 核對。

## 5. Coding / formatting

- 新增或修改的 C、Java、Ant code 使用 Tab。
- **前大括弧 `{` 一律換行**：

```text
int main()
{
	...
}
```

- 未修改的 upstream source 不因本規則重新排版；已手動標準化的 migrated source 必須記錄為 `format-normalized`，不得稱 `byte-identical`。

## 6. C/Java integration gate

設計原則：**C network/runtime + Java Servlet execution**，不是把 Tomcat 翻成 C。C 優先處理 socket、accept、event loop、readiness、connection state、I/O buffer、適合 native 化的 syscall-intensive path、TLS integration、back-pressure、native resource lifecycle；Java 保留 Servlet API、application lifecycle、dispatch、Filter/Listener/Session、ClassLoader、JSP/Jasper 與 application semantics。每個 boundary 定義 representation、ownership、mutability、lifetime、thread affinity、ABI、error semantics。

固定 gate：

```text
Native event
  ↓
stable native handle ↔ Java transport object
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

- stable handle 必須有可驗證 registry owner/lifetime；不得依賴暫時 Java map/callback lifetime。
- `SocketWrapperBase` 必須是 pinned exact source；shell 不算等價。
- `processSocket()` / `SocketProcessorBase` 必須遵守 pinned Tomcat dispatch、executor、locking、lifecycle contract；surface test 不能取代 integration。
- Java enqueue ≠ transport consumption；worker 不得直接操作 event-loop-owned epoll registration。
- interest/rearm/close 最終決定權屬 native event-loop owner；每個 event-processing cycle 恰好一次 rearm 或 close。
- `kernel readiness` ≠ Servlet `isReady()`；`event dispatched` ≠ `event consumed`；`Java task submitted` ≠ `SocketProcessor processed`。
- 真實 `SocketWrapper → processSocket → SocketProcessor → ProtocolHandler` 建立前，不得跳到 `Http11Processor` 或 Servlet integration。

## 7. Ownership、request/response、C 化決策

每個 native handle 定義 create/transfer/borrow/return/destroy；每個資料結構定義 owner、lifetime、thread、sync、copy/no-copy、error、cancel、cleanup、back-pressure。不得以「應該不會發生」合理化 NULL、overflow、UAF、double-free、race。

Request/response flow 必須以 source 對照：
`Client → TCP/TLS → socket → accept → event loop → connection state → read buffer → HTTP parser → Request metadata → mapping → Context/Wrapper → Filter chain → Servlet.service() → response buffer → HTTP encoding/chunking → TLS → socket write → keep-alive/close`。

候選 C 化元件至少評估 crossing、hot-path、allocation/GC、copy/bandwidth、syscall、lock/atomic/context switch、JIT 可最佳化程度、complexity、thread-safety、ownership/lifetime、安全、維護與 benchmarkability。若 C 化增加 crossing/copy/sync/lifecycle complexity，預設不 C 化；Servlet/application semantics 預設 Java 優先。

## 8. Build、test、安全與證據

- **Ant 是唯一正式 build/test orchestrator**；沿用 pinned Tomcat build 架構，不以 Maven/Gradle/CMake/Meson 取代。
- Native compile/link、Java compile、tests、Servlet tests、benchmark 均須可由 Ant 驅動。
- 測試前確認 OS/arch、JDK/JAVA_HOME、Ant、compiler/linker、JNI headers、OpenSSL、system libs、library path、port、dependencies、permissions；環境未完成則 fail，不進 benchmark。
- 分開記錄 Tomcat tests、NativeTomcat tests、Servlet TCK、benchmark。
- 安全至少覆蓋 parsing、URI/header validation、size/timeout/keep-alive/slowloris、memory/integer safety、race/UAF/double-free、TLS/cert/ALPN/HTTP2、request smuggling、path traversal、malformed input、resource exhaustion、shutdown/cancellation/cleanup。

最高已驗證層級：`source-verified` → `implemented` → `compiled` → `unit-tested` → `integration-tested` → `Servlet/TCK-tested` → `benchmark-verified`。

可另標 `specification-verified`、`theoretical analysis`，但不能取代 runtime verification；低層級結果不得升級成高層級結論。

Benchmark 至少記錄 latency（平均/P50/P95/P99/P99.9）、throughput、connections、keep-alive、RSS/heap/native memory、allocation、GC、syscall、context switch、lock contention、CPU、JNI/FFM crossing、bytes copied、TLS cost、error rate、overload tail latency，並在相同硬體/OS/JDK/compiler/workload 下比較。若 C 化較慢，保留結果並修改架構，不扭曲結論。

## 9. 工作記錄與 roadmap

每完成重要步驟記錄：修改檔案、source/spec/version/commit、採用 contract、code/docs/steps 修正、最高 verification level、本機/線上 diff 狀態、remaining/blocked/deferred。

第一階段目標是可驗證的最小 end-to-end path，不是一次重寫 Tomcat；固定 gate 不得跳過。目前順序：

`SocketWrapperBase → supporting source closure → NativeSocketWrapper → NioEndpoint.NioSocketWrapper/Poller/processKey → real processSocket/SocketProcessorBase → transport consumption → Http11Processor → CoyoteAdapter/Catalina/Servlet`

第二階段才處理 Async、non-blocking I/O、TLS、HTTP/2、session、JSP/Jasper、advanced buffering、zero-copy、back-pressure、connection scaling 等尚未由當前 gate 證明必要的複雜功能。

最終目標：C 僅負責有可證明成本優勢的 network/I/O/runtime path，Java/HotSpot 保留 Servlet/application semantics，以可重現測試證明相容性與效能；不是「用 C 重寫 Tomcat」。