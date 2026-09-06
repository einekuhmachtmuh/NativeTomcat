# NativeTomcat 工作基本原則

本文件是後續 NativeTomcat 工程工作的強制守則。它約束的是**工作方法**，不是固定 roadmap；每次提出新步驟、修正或架構調整，都必須重新核驗現況。

## 1. 三方核對，外部裁決

每次提出新步驟前，依序核對：

1. **實際程式碼與測試**：確認目前 C/C++、Java、build/test 及真實行為。
2. **目前 docs**：確認 architecture、ownership、lifecycle、event、buffer、C/Java boundary、Tomcat integration、source migration 與 build/test 描述，並區分 `implemented / source-verified / tested / deferred / blocked`。
3. **新步驟**：確認它是否建立在已核實的現況上，是否跳過必要 gate，或與既有 contract 衝突。

若三者不一致，先指出差異，再決定修改 code、docs 或步驟；不得為維持舊 roadmap 而硬接續。

三方本身都不是技術真理，爭議必須回到對應外部來源：

- **Servlet 6.1 規範**：裁決 application-visible Servlet semantics。
- **Apache Tomcat 11.0.25 pinned source**：裁決 Tomcat implementation、integration path、class/method contract、ownership、lifecycle、locking、executor、buffer、event、HTTP processing 等。Pinned commit：`cbe6e15ee81e2fc6232954292a80cca5d1e84009`。
- **NGINX source**：裁決與交叉驗證 native event architecture，包括 readiness、registration/rearming、connection ownership、event/handler separation、write-interest 等。

NGINX 不取代 Tomcat 作為 Java/Tomcat baseline；Tomcat implementation detail 也不能取代 Servlet specification。

## 2. 固定核驗流程

### A. 查實際現況

檢查相關 implementation、tests 與 build/test path；至少包括 native runtime/connection、event/rearm、JNI、`NativeSocketWrapper`、`SocketWrapperBase`、`AbstractEndpoint.processSocket`、`SocketProcessorBase`、`ProtocolHandler`、`build.xml`、`build_test.xml`。

必須分清「source 中存在」與「已可運作、已測試」。

### B. 查全部相關 docs

不要只看預計修改的文件。若 docs 互相矛盾，不以文件的新舊、篇幅或 roadmap 裁決，回到 source verification。

### C. 查 pinned Tomcat source

凡涉及 Tomcat Java source 或 execution path，必須核對 method body、真實呼叫鏈、ownership/lifetime、locking、executor/thread model、event mapping、buffer/read/write semantics、error/close/recycle 與 abstract/subclass contract。

同時遵守 `docs/tomcat-source-migration-rule.md`：若 implementation materially involves Tomcat Java source，先確認 repo `java/` 是否已有 exact package/path source；沒有就先遷入 pinned source，不得以記憶或 API 文件手寫替代。若 exact source 尚未取得或無法核驗，標記為 **source verification blocked/incomplete**，不得宣稱 migration 或等價性完成。

### D. 查 NGINX source

凡涉及 native event loop、read/write readiness、registration/rearming、connection lifecycle 或 event/handler separation，都要對照 NGINX 對應 source。

始終分開：

`kernel readiness → native event handling → transport consumption → Java/Tomcat processing → Servlet readiness`

不得把其中任兩層視為同一語意。

### E. 重新對齊

source verification 後重新調整：

- **code**：符合確定的 contract；
- **docs**：反映實際且已驗證的狀態；
- **steps**：依新狀態排列，不跳過必要 gate。

若只有 docs 或步驟需要改，也要說明 source 為何支持該修正。

## 3. 不得跳過的 gate

提出或執行下一步前，檢查：

1. source verification；
2. 必要的 exact upstream Java source；
3. supporting classes；
4. ownership/lifecycle contract；
5. transport contract；
6. Tomcat integration boundary；
7. build/test path；
8. 前置測試。

若外部環境造成 blocking，可以先做不依賴該 gate 的獨立工作，但不得把 blocked gate 宣稱完成，也不得用 shell test 冒充 exact upstream integration。

## 4. 必須維持的語意邊界

- 不得以 NativeTomcat shell、docs 或 roadmap 作為 Tomcat semantics 的權威來源。
- 不得把 NGINX implementation detail 提升為 Servlet 或 Tomcat requirement。
- 不得在尚未建立真實 `SocketWrapper → processSocket → SocketProcessor → ProtocolHandler` 路徑前跳到 `Http11Processor` 或 Servlet。
- `kernel readiness` 不等於 Servlet `isReady()`。
- `event dispatched` 不等於 `event consumed`。
- `Java task submitted` 不等於 `SocketProcessor processed`。
- `EPOLLONESHOT` 下，不能因 Java work 已 enqueue 就任意 rearm；rearm ownership 必須與實際 consumption contract 一致。
- 不得把未取得、未核驗的 pinned source 宣稱為 migration 完成。
- 不得因 compile 或 surface test PASS 就推定 Tomcat runtime semantics 正確。

## 5. 驗證分層

每項工作明確標示已達到的層級：

1. source verified
2. code implemented
3. compilation verified
4. unit test verified
5. integration verified
6. Servlet/TCK verified
7. benchmark verified

必要時可另標示 specification-verified 或 theoretical analysis，但不能取代 runtime verification。

例如：surface test PASS ≠ Tomcat integration PASS；compile PASS ≠ runtime semantics correct；native event test PASS ≠ Servlet readiness correct；benchmark harness 可執行 ≠ benchmark result 已取得。

## 6. 工作記錄

每完成重要步驟，記錄：

- 修改檔案；
- 核對的 Tomcat / NGINX / Servlet source、版本與 commit/spec 版本；
- 採用的 contract；
- code、docs、steps 的修正；
- compile/test/verification 結果及其 verification level；
- 尚未完成、blocked 或 deferred 的部分。

本文件本身也受上述規則約束。若後續 source verification 證明其中規則有誤，依同一流程修訂，不把本文件視為不可更改的技術真理。
