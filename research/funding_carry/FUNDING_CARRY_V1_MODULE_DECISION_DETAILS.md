# Funding Carry V1：策略模組決策細節（Code-Level）

本文件描述目前 **Funding Carry V1** 在程式中的實際決策流程。

- 主要程式碼：
`QTrading.Universe/src/FixedUniverseSelector.cpp`
`QTrading.Signal/src/FundingCarrySignalEngine.cpp`
`QTrading.Intent/src/FundingCarryIntentBuilder.cpp`
`QTrading.Risk/src/SimpleRiskEngine.cpp`
`QTrading.Execution/src/FundingCarryExecutionOrchestrator.cpp`
`QTrading.Execution/src/LiquidityAwareExecutionScheduler.cpp`
`QTrading.Execution/src/MarketExecutionEngine.cpp`
`QTrading.Execution/src/FundingCarryExchangeGateway.cpp`
`QTrading.Execution/src/FundingCarryStrategy.cpp`
`QTrading.Monitoring/src/SimpleMonitoring.cpp`

- 目前策略設定來源：
`research/funding_carry/config/funding_carry_v1.json`

- 注意：本版已移除策略模組內 env sweep 覆寫，參數以 config + 預設值為主。

---

## 0. 整體資料流（每個 tick）

在 `FundingCarryStrategy::wait_for_done()` 的順序如下：

1. 從 market channel 取最新 `MultiKlineDto`
2. `Universe.select()`（目前固定 universe）
3. `Signal.on_market(market)`
4. `Intent.build(signal, market)`
5. `Gateway.BuildAccountState()`（拿 positions/open_orders/spot/perp/total cash）
6. `Risk.position(intent, account, market)`
7. `ExecutionOrchestrator.Execute(risk, account, signal, market)`
8. `Gateway.SubmitOrders(orders)`
9. `Monitoring.check(account)` -> `Gateway.ApplyMonitoringAlerts(alerts)`

這是單向 pipeline，Service 只負責「每步驅動」與模組組裝。

---

## 1. Universe 模組（可交易標的集合）

檔案：`QTrading.Universe/src/FixedUniverseSelector.cpp`

### 決策

- 預設回傳固定兩腿：`BTCUSDT_SPOT` + `BTCUSDT_PERP`
- 若建構時有傳 symbols，則用外部傳入清單

### 輸出

- `UniverseSelection.universe`（string list）

### 目前特性

- 沒有動態選幣、沒有 liquidity/funding ranking
- 等於「固定 BTC carry 實驗模式」

---

## 2. Signal 模組（FundingCarrySignalEngine）

檔案：`QTrading.Signal/src/FundingCarrySignalEngine.cpp`

### 核心角色

Signal 不是做短線方向交易，而是決定：

- 這個 tick 是否允許持有 carry（Active/Inactive）
- 若 Active，給出 `confidence`（影響後續風險 sizing）

### 輸入

- 市場價格：spot/perp close
- perp funding snapshot（若有）
- 內部狀態：cooldown、streak、adaptive 歷史窗

### 關鍵計算

1. **資料完整性 gate**
- spot/perp 任一缺失 -> `Inactive`

2. **basis 與 proxy funding**
- `basis_pct = (perp - spot) / spot`
- `funding_proxy = basis_pct * 0.25`

3. **observed funding 優先**
- 若 funding snapshot 在有效時間窗內：用 observed funding
- 否則 fallback 到 basis proxy

4. **funding nowcast（可選）**
- 用上次 settlement rate 往 proxy 線性過渡
- 可分別控制是否用於 entry/exit gate、confidence

5. **adaptive 門檻（可選）**
- funding quantile -> entry/exit funding gate
- basis quantile -> entry/exit basis gate
- 支援 refresh interval、floor/cap

6. **regime persistence（可選）**
- 透過 funding sign persistence 判斷：trending / neutral / choppy
- 動態調整 entry/exit persistence

7. **entry/exit gate**
- entry：funding streak 達標 + basis 在門檻內 + cooldown OK
- exit：funding 壞 streak 或 basis 超標
- hard-negative funding 可緊急退出
- pre-settlement negative projection 可提前退出（附 re-entry block）
- inactivity watchdog 可避免長期 0 交易

8. **confidence**
- 基礎：`funding_score * basis_score`（0~1）
- 再乘上 adaptive confidence multiplier（可選）
- 再乘上 adaptive structure multiplier（可選）
- urgency 固定 `Low`

### 輸出

- `SignalDecision`
  - `status`: Active/Inactive
  - `confidence`: 0~1
  - `strategy`: `funding_carry`
  - `urgency`: Low

---

## 3. Intent 模組（FundingCarryIntentBuilder）

檔案：`QTrading.Intent/src/FundingCarryIntentBuilder.cpp`

### 核心角色

把 Signal 狀態轉成「結構意圖」，不做 sizing。

### 決策

- 若 `signal.status != Active`：回傳空 legs（表示應平倉/不持有）
- 若 Active：產生兩腿 delta-neutral intent
  - `receive_funding = true`（預設）
  - 腿為：Long Spot + Short Perp

### 輸出

- `TradeIntent`
  - `structure = delta_neutral_carry`
  - `position_mode = hedge`
  - `legs = [spot long, perp short]`（或反向）
  - `confidence` 直接承接 signal

---

## 4. Risk 模組（SimpleRiskEngine）

檔案：`QTrading.Risk/src/SimpleRiskEngine.cpp`

### 核心角色

把「結構意圖」轉成「每腿目標 notional + leverage」。

### A. 空 intent 處理

- `intent.legs.empty()` 時：所有現有倉位 target 設 0（平倉路徑）

### B. Carry sizing 主路徑

僅在 carry 且 market 可用時進入。

1. 計算目前持倉 notional（每腿）
- `gross = Σ|notional|`
- `net = Σ signed_notional`

2. 目標腿名目（target_leg_notional）
- 起點：`notional_usdt`
- 可由 `total_cash_balance` 放大（auto notional / allocator model）
- 可用 EMA 平滑 auto target
- 再受 spot/perp 可用資金容量限制
- 可受 perp liquidation buffer guard 再降尺度

3. 置信度大小映射
- base scale：`carry_confidence_min/max_scale + power`
- 可選 core+overlay 模型
- 可選 confidence boost（目前 V1 開啟）
  - 參考點 `carry_confidence_boost_reference`
  - 最大額外倍率 `carry_confidence_boost_max_scale`
  - 曲線 `carry_confidence_boost_power`

4. 再平衡抑制（降低 churn）
- 若 `|net|/gross < rebalance_threshold_ratio` 且沒有 gross deviation 失衡
- 直接回傳「維持當前 notional」

5. basis 相關風控縮放
- basis soft cap
- negative basis hysteresis scaling
- basis overlay（相對 basis EMA 偏移）
- basis trend slope scaling

6. 輸出 target_positions
- 先算共同 `base_qty`，再換回每腿 notional（保證兩腿對齊）
- perp leverage 可依 confidence scale

7. size economics gate（可選）
- 比較預估 funding gain vs 交易成本
- 若不划算，回退到 current positions（不調倉）

### 輸出

- `RiskTarget.target_positions[symbol] = signed notional`
- `RiskTarget.leverage[symbol]`
- `RiskTarget.max_leverage`

---

## 5. Execution Orchestrator（父單 -> 切片 -> 下單）

檔案：`QTrading.Execution/src/FundingCarryExecutionOrchestrator.cpp`

### 決策流程

1. `RiskTarget` 轉 `ExecutionParentOrder`（每 symbol 一筆目標）
2. 交給 Scheduler 做切片（可限制每 tick delta）
3. 交給 Policy 產出 execution target
4. 交給 ExecutionEngine 轉為具體訂單

Orchestrator 本身不做行情判斷，只做流程編排。

---

## 6. Scheduler（LiquidityAwareExecutionScheduler）

檔案：`QTrading.Execution/src/LiquidityAwareExecutionScheduler.cpp`

### 核心角色

限制單步調倉量，降低大資金衝擊與過度 churn。

### 主要功能（由 config 控制）

- per-bar quote volume participation cap
- confidence adaptive rate
- gap adaptive rate
- window budget cap
- increase batching（只延後「加倉」，減倉不延）

### 目前 V1 實際狀態

- `FundingCarryStrategy` 以預設建構 `execution_scheduler_()`
- 預設大多數功能是關閉（需要顯式 config 才啟用）
- 所以當前主要靠 `MarketExecutionEngine` 本身控制節奏

---

## 7. Execution Engine（MarketExecutionEngine）

檔案：`QTrading.Execution/src/MarketExecutionEngine.cpp`

### 核心角色

把 target notional 差值轉換成實際 order。

### 主要決策

1. 建立 symbol->price 快取，計算當前持倉 notional
2. 對 carry（low urgency）啟用一組特殊控制：
- cooldown（避免高頻重複修正）
- step ratio（單步最大調整比例）
- large-notional 更保守 step/cooldown
- participation cap（依 quote volume）
- window budget（可選）
- max rebalances/day（可選）
- min notional（過小不下單）

3. 兩腿協調控制
- `carry_require_two_sided_rebalance`
- `carry_balance_two_sided_rebalance`（可剪裁大的一腿）

4. maker-first（目前 V1 開啟）
- 當 gap ratio 小於 `carry_maker_catchup_gap_ratio`
- 用 limit（`carry_maker_limit_offset_bps`）
- gap 太大則 fallback market（追價）

5. target anchor（可選）
- 抑制小幅 target 抖動造成頻繁調倉

### 輸出

- `ExecutionOrder`（Buy/Sell, qty, Market/Limit, reduce_only）

---

## 8. Exchange Gateway（下單路由與賬戶快照）

檔案：`QTrading.Execution/src/FundingCarryExchangeGateway.cpp`

### 核心角色

把 strategy 執行與交易所模擬器 API 解耦。

### 決策

- 依 instrument type（顯式 map）分流到 `spot.place_order` 或 `perp.place_order`
- Monitoring alert 為 `CANCEL_OPEN_ORDERS` 時，依型別取消對應市場 open orders

---

## 9. Monitoring 模組（SimpleMonitoring）

檔案：`QTrading.Monitoring/src/SimpleMonitoring.cpp`

### 決策

- 按 symbol 統計 open orders
- 若超過 `max_open_orders_per_symbol`：
  - 產生 `OPEN_ORDERS_EXCEEDED`
  - action = `CANCEL_OPEN_ORDERS`

這是純防呆風險，不涉及 alpha。

---

## 10. 目前 V1 啟用中的關鍵參數（來自 json）

檔案：`research/funding_carry/config/funding_carry_v1.json`

目前有顯式設定的只有：

- Risk
  - `carry_confidence_boost_enabled = true`
  - `carry_confidence_boost_reference = 0.56`
  - `carry_confidence_boost_max_scale = 0.18`
  - `carry_confidence_boost_power = 1.6`

- Execution
  - `carry_maker_first_enabled = true`
  - `carry_maker_limit_offset_bps = 1.0`
  - `carry_maker_catchup_gap_ratio = 0.55`

其餘 signal/intent/risk/execution/monitoring 參數採各模組預設值。

---

## 11. 模組責任邊界（你現在可以這樣理解）

- Signal：可不可以持有、信心多少
- Intent：持有哪兩腿、方向是什麼
- Risk：每腿要多大（notional/leverage）
- Scheduler：單 tick 調多少（切片/節流）
- ExecutionEngine：下什麼訂單（market vs limit、qty、reduce_only）
- Gateway：送單到 spot/perp + 監控動作執行
- Monitoring：保護系統（例如 open order 爆量）

這個邊界就是之後做 P2/P3 結構優化時最重要的改動座標。
