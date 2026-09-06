# NativeTomcat 工作基本原則

## 目的

本文件是 NativeTomcat 後續工程工作的強制性工作原則。每當 Codex 提出新的實作步驟、修正方案、架構調整或驗證順序時，不能只根據先前的計畫或目前文件內容推進；必須重新核對「實際程式碼、docs、提出的步驟」三者是否一致。

## 三方核對原則

每次提出新的步驟建議前，必須依序檢查：

1. **目前已實作的程式碼**：確認實際存在的 C/C++、Java、build/test code 與其真實行為；不得以舊計畫、註解或記憶代替程式碼現況。
2. **目前 docs**：確認相關 ownership、lifecycle、event、buffer、C/Java boundary、Tomcat integration 與 source-migration contract 是否仍與實作一致。
3. **新的步驟建議**：逐項檢查建議是否真的建立在前兩者之上，是否會跳過尚未完成的 integration gate，或與既有 contract 互相矛盾。

若三者有落差，必須先找出落差，再決定修改哪一方；不得直接沿用原先步驟。

## 最終裁決依據

三方出現爭議時，**Apache Tomcat 及 NGINX 的對應原始碼是最終裁決依據**：

- Tomcat 使用專案所固定的 pinned baseline；目前 baseline 為 Tomcat 11.0.25 commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`。
- NGINX 是 event-driven native architecture 的交叉驗證來源，用來核對 event backend、readiness、handler dispatch、connection ownership、write-interest 等邊界。
- 不得以目前 NativeTomcat 自己的 shell、暫時實作、docs 或先前提出的設計，反過來證明該設計就是正確的。
- 如 Tomcat/NGINX 原始碼與 NativeTomcat 現有實作或 docs 不一致，必須先判斷這是否是有意的架構差異；若沒有經過明確、可驗證的理由，不得把 NativeTomcat 現況視為正確答案。

## 每次新步驟建議的固定流程

### A. 現況核驗

先檢查涉及的實際程式碼與測試結果，包括但不限於：

- native runtime / connection ownership；
- event registration、readiness、rearm；
- JNI boundary；
- NativeSocketWrapper / SocketWrapperBase；
- AbstractEndpoint / processSocket；
- SocketProcessorBase / ProtocolHandler；
- build.xml / build_test.xml；
- 已存在的 unit、integration 或其他驗證。

### B. Docs 核驗

檢查與該步驟相關的 docs，確認文件描述的是：

- 已實作行為；
- 已驗證但尚未完整實作的 contract；或
- 明確標示的 future/deferred design。

不得把 future design 誤報成 implemented behavior，也不得因實作暫時存在而默認其 contract 正確。

### C. Tomcat 原始碼核驗

凡涉及 Tomcat Java source 或 Tomcat execution path，必須先對照 pinned Tomcat source。尤其要核對實際呼叫鏈、ownership、lifecycle、locking、executor、event mapping、buffer semantics 及 error handling，而不是只比較 class/method 名稱。

仍必須遵守 `docs/tomcat-source-migration-rule.md`：若 implementation materially involves a Tomcat Java source file，先確認 repo `java/` 是否已有該原始 package/path 的 exact source；沒有則應先遷入 pinned source，不得以記憶或 API 文件手寫替代。

### D. NGINX 原始碼交叉核驗

凡涉及 native event loop、read/write readiness、event registration/rearming、connection lifecycle 或 event/handler separation，應對照 NGINX 對應 event source，以確認 NativeTomcat 是否錯把 kernel readiness、transport consumption、application processing 或 Servlet readiness 混為同一層。

NGINX 是架構交叉驗證依據，不是 NativeTomcat 的 Java/Tomcat implementation baseline。

### E. 修訂三者

核驗後，必須使三者重新一致：

1. **程式碼**：修正實際 implementation，使其符合確定的 contract；
2. **docs**：更新為實際、已驗證的狀態，明確區分 implemented / verified / deferred；
3. **步驟**：重新排列後續工作，確保不跳過尚未完成的 ownership、lifecycle、transport、Tomcat integration 或 verification gate。

若應修改的是 docs 而不是 code，必須明確說明為何原始碼核驗後確認原先 docs/步驟的描述不正確。反之亦然。

## 不得跳過的驗證分層

每個階段必須明確標示目前達到哪一層：

1. source verified；
2. code implemented；
3. compilation verified；
4. unit test verified；
5. integration verified；
6. Servlet/TCK verified；
7. benchmark verified。

較低層級的成功不得被描述成較高層級的成功。例如 surface test PASS 不代表 Tomcat integration PASS；compile PASS 不代表 runtime semantics 正確；architecture review PASS 不代表 TCK PASS。

## 核心禁止事項

- 不得以「之前的 roadmap」代替重新檢查目前程式碼。
- 不得以 docs 的描述證明 docs 自己正確。
- 不得以目前 NativeTomcat 的 shell implementation 作為 Tomcat semantics 的權威來源。
- 不得在尚未建立 `SocketWrapper → processSocket → SocketProcessor → ProtocolHandler` 真實路徑前直接跳到 `Http11Processor` 或 Servlet。
- 不得把 native kernel readiness 直接等同於 Servlet `isReady()`。
- 使用 `EPOLLONESHOT` 時，不得在 Java work 僅被 enqueue、尚未完成其指定 consumption obligation 時就任意 rearm；rearm ownership 必須與實際 event-processing contract 一致。
- 不得把未取得或未核驗的 pinned upstream source 宣稱為已完成 migration。

## 工作記錄要求

每次完成一個重要步驟後，應留下可追溯的結果：

- 修改了哪些檔案；
- 哪些 Tomcat/NGINX 原始碼被核對；
- 核對後採用了什麼 contract；
- 程式碼、docs、roadmap 各自修正了什麼；
- 執行了哪些 compile/test/verification；
- 尚有哪些明確未完成或被 deferred 的部分。

本文件本身也受上述三方核對原則約束；若後續原始碼核驗證明本文件的規則有誤，應依相同流程修訂，而不是把本文件視為不可更改的技術真理。
