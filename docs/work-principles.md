# NativeTomcat 工作基本原則

## 目的

本文件是 NativeTomcat 後續工程工作的強制性工作原則。每當 Codex 提出新的實作步驟、修正方案、架構調整或驗證順序時，不能只根據先前的計畫或目前文件內容推進；必須重新核對「實際程式碼、docs、提出的步驟」三者是否一致，並以對應的上游原始碼與規範裁決爭議。

本原則的目的不是把 roadmap 固定下來，而是讓 roadmap 隨著實際實作與 source verification 持續修正，避免「舊計畫 → 新計畫 → 再用新計畫證明舊決策」的循環。

## 三方核對原則

每次提出新的步驟建議前，必須重新檢查三個對象：

1. **目前已實作的程式碼**：確認實際存在的 C/C++、Java、build/test code、測試與其真實行為；不得以舊計畫、註解、文件或記憶代替程式碼現況。
2. **目前 docs**：逐一確認與該步驟相關的 architecture、ownership、lifecycle、event、buffer、C/Java boundary、Tomcat integration、build/test 與 source-migration contract；不得把文件中的 future/deferred design 當成已實作事實。
3. **新的步驟建議**：確認建議是否建立在前兩者的已核實狀態上，是否跳過尚未完成的 integration/ownership/verification gate，或是否與既有 contract 衝突。

三者若有落差，必須先明確列出落差，再決定應修改程式碼、docs 或步驟；不得為了維持原 roadmap 而忽略落差。

## 最終裁決依據

三方出現技術爭議時，不以三方中的任何一方自行證明自己正確；必須回到相應的外部權威來源。**三方核對框架保持不變，但最終裁決來源依問題所在層次區分。**

### Tomcat 裁決範圍

凡涉及 Tomcat implementation behavior、呼叫鏈、class/method contract、ownership、lifecycle、locking、executor、buffer semantics、SocketWrapper、Endpoint、ProtocolHandler 或 HTTP processing path，**Apache Tomcat 11.0.25 pinned source 是最終實作裁決依據**：

- baseline：Tomcat 11.0.25
- pinned commit：`cbe6e15ee81e2fc6232954292a80cca5d1e84009`
- 不得以 Tomcat main 分支未經版本核對的現況取代 pinned baseline。
- 不得以 NativeTomcat shell、docs、既有 roadmap 或記憶推導出「Tomcat 應該如此運作」。

### NGINX 裁決範圍

凡涉及 native event architecture、event backend、readiness、event registration/rearming、connection ownership、event/handler separation、write-interest 等 native event-loop 問題，**NGINX 對應原始碼是架構交叉驗證與裁決依據**。

NGINX 不取代 Tomcat 作為 Java/Tomcat semantics 的基準，也不得把 NGINX implementation detail 直接視為 Servlet container requirement。

### Servlet 規範邊界

凡涉及 Servlet API 的 application-visible semantics，必須另以 Jakarta Servlet 6.1 規範核對。Tomcat 與 NGINX 原始碼都不能把自身 implementation detail 提升為 Servlet specification requirement。

因此：

- **Servlet 規範**裁決 application-visible contract；
- **Tomcat pinned source**裁決目標 Tomcat implementation path 與 integration contract；
- **NGINX source**裁決 native event architecture 的交叉正確性；
- NativeTomcat 的 code、docs、steps 三者都必須依上述來源重新校正。

## 每次新步驟建議的固定流程

### A. 現況核驗：程式碼

先檢查涉及的實際 implementation、測試與 build/test path，包括但不限於：

- native runtime / connection ownership；
- event registration、readiness、rearm；
- JNI boundary；
- NativeSocketWrapper / SocketWrapperBase；
- AbstractEndpoint / processSocket；
- SocketProcessorBase / ProtocolHandler；
- build.xml / build_test.xml；
- 已存在的 unit、integration 或其他 verification。

必須區分「存在於 source」與「實際可運作／已測試」兩件事。

### B. Docs 核驗：文件

不要只閱讀預計修改的單一文件；必須檢查所有會影響該步驟的相關 docs，尤其是 ownership、event、buffer、C/Java boundary、Tomcat integration、source migration 與 build/test 文件。

對每個相關描述標記其性質：

- **implemented**：程式碼已存在；
- **source-verified**：已由指定上游原始碼核實；
- **tested**：已有相應測試結果；
- **deferred/future**：尚未實作；
- **blocked**：有明確外部條件阻塞。

若不同 docs 互相矛盾，不能以較新的文件、較詳細的文件或 roadmap 自動裁決；必須進入 source verification。

### C. Tomcat 原始碼核驗

凡涉及 Tomcat Java source 或 Tomcat execution path，必須先對照 pinned Tomcat source。不能只比較 class/method 名稱，至少要核對：

- 真實呼叫鏈；
- method body；
- ownership 與 lifetime；
- locking / synchronization；
- executor / thread model；
- event mapping；
- buffer / read / write semantics；
- error / close / recycle path；
- subclass / abstract-method contract。

仍必須遵守 `docs/tomcat-source-migration-rule.md`：若 implementation materially involves a Tomcat Java source file，先確認 repo `java/` 是否已有該原始 package/path 的 exact source；沒有則應先遷入 pinned source，不得以記憶或 API 文件手寫替代。

若 exact pinned source 尚未取得或無法完成核驗，必須明確標示為 **source verification blocked/incomplete**，不得把 shell 或 API documentation 當成等價證據。

### D. NGINX 原始碼交叉核驗

凡涉及 native event loop、read/write readiness、event registration/rearming、connection lifecycle 或 event/handler separation，應對照 NGINX 對應 event source。

特別要分開：

`kernel readiness → native event handling → transport consumption → Java/Tomcat processing → Servlet readiness`

不得把其中任何兩層僅因名稱相似而視為同一語意。

NGINX 是 native architecture 的交叉驗證依據，不是 NativeTomcat 的 Java/Tomcat implementation baseline。

### E. 三者重新校正

完成 source verification 後，必須重新使三者一致：

1. **程式碼**：修正 implementation，使其符合已確定的 contract；
2. **docs**：更新為實際且已驗證的狀態，明確區分 implemented / verified / deferred / blocked；
3. **步驟**：依新的 source/code/docs 狀態重新排列後續工作，不跳過尚未完成的 gate。

若核驗結果顯示應修改 docs 而非 code，必須指出外部 source 為何支持 docs 原先的方向；反之亦然。若三者都需要改，必須一次說清楚三者如何重新對齊。

## 依賴與阻塞規則

新的步驟不得只因「理論上可以先做」就越過必要依賴。至少必須檢查：

1. source verification 是否完成；
2. 必要的 exact upstream Java source 是否已取得；
3. supporting classes 是否已遷移或核實；
4. ownership/lifecycle contract 是否固定；
5. transport contract 是否固定；
6. Tomcat integration boundary 是否核實；
7. build/test path 是否存在；
8. 前置測試是否通過。

如果某一 gate 被外部環境阻塞，可以把後續工作拆成不依賴該 gate 的獨立工作，但不得把 blocked gate 本身標記為完成，也不得以替代性 shell test 宣稱已完成 exact upstream integration。

## 不得跳過的驗證分層

每個階段必須明確標示目前達到哪一層：

1. **source verified**；
2. **code implemented**；
3. **compilation verified**；
4. **unit test verified**；
5. **integration verified**；
6. **Servlet/TCK verified**；
7. **benchmark verified**。

必要時可另外標示 specification-verified 與 theoretical analysis，但不能用它們取代 runtime verification。

較低層級的成功不得被描述成較高層級的成功。例如：

- surface test PASS ≠ Tomcat integration PASS；
- compile PASS ≠ runtime semantics correct；
- native event test PASS ≠ Servlet readiness correct；
- architecture review PASS ≠ TCK PASS；
- benchmark harness 可執行 ≠ benchmark result 已取得。

## 核心禁止事項

- 不得以「之前的 roadmap」代替重新檢查目前程式碼、docs 與 source。
- 不得以 docs 的描述證明 docs 自己正確。
- 不得以目前 NativeTomcat 的 shell implementation 作為 Tomcat semantics 的權威來源。
- 不得把 NGINX implementation detail 直接提升為 Servlet requirement 或 Tomcat implementation requirement。
- 不得在尚未建立 `SocketWrapper → processSocket → SocketProcessor → ProtocolHandler` 真實路徑前直接跳到 `Http11Processor` 或 Servlet。
- 不得把 native kernel readiness 直接等同於 Servlet `isReady()`。
- 使用 `EPOLLONESHOT` 時，不得在 Java work 僅被 enqueue、尚未完成其指定 consumption obligation 時就任意 rearm；rearm ownership 必須與實際 event-processing contract 一致。
- 不得把「event 已 dispatch」誤寫成「event 已 consumed」。
- 不得把「Java task 已提交」誤寫成「Tomcat SocketProcessor 已處理」。
- 不得把未取得或未核驗的 pinned upstream source 宣稱為已完成 migration。
- 不得因 docs、roadmap 或測試名稱看起來合理，就跳過 source-level verification。
- 不得因目前程式碼能編譯，就推定其與 Tomcat semantics 等價。

## 工作記錄要求

每次完成一個重要步驟後，應留下可追溯的結果：

- 修改了哪些檔案；
- 哪些 Tomcat / NGINX / Servlet source 被核對；
- 使用的版本、tag/commit 或規範版本；
- 核對後採用了什麼 contract；
- 程式碼、docs、roadmap 各自修正了什麼；
- 執行了哪些 compile/test/verification；
- 每項結果屬於哪一個 verification level；
- 尚有哪些明確未完成、blocked 或 deferred 的部分。

本文件本身也受上述三方核對原則約束；若後續 source verification 證明本文件的規則有誤，應依相同流程修訂，而不是把本文件視為不可更改的技術真理。
