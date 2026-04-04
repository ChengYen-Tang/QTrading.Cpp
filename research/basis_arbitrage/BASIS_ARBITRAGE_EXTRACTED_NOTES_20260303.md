# Basis Arbitrage 摘要筆記（取自 `deep-research-report.md`）

## 1. 範圍與界定

包含內容：
1. Basis 定義
2. Basis 單獨策略家族
3. 僅 basis 研究所需的訊號、尺寸、再平衡與風控邏輯
4. 第二章 `Basis Standalone` 的模擬與研究方向

排除內容：
1. Funding carry 當主策略的設計
2. Funding carry 與 basis 的整合框架
3. 多標的 universe / multi-symbol 資金分配

目標是先建立清楚的 **Basis Standalone** 基線，再考慮未來章節的 funding carry 整合。

目前階段的研究標的固定為：
1. `BTCUSDT` 現貨
2. `BTCUSDT_PERP` 永續

也就是說，本階段 **不做 multi-symbol 擴展**，先把單一標的的資料語義、模擬一致性、訊號、風控與 execution 問題研究清楚。

---

## 2. Basis 定義（採用雙層）

並行使用兩種 basis 指標，因為執行與風險需要不同的錨點。

### 2.1 交易 Basis（執行層）

採用可交易價格：

`basis_trade_t = (perp_mid_t - spot_mid_t) / spot_mid_t`

或用 bid/ask 版本建模保守成交。

這個 basis 用於：
1. 進出場判斷
2. 成本後優勢評估
3. 實際交易可得 edge 的估計

### 2.2 風險 Basis（保證金 / 清算層）

採用 mark-index 定義：

`basis_risk_t = (perp_mark_t - perp_index_t) / perp_index_t`

這個 basis 用於：
1. 對齊交易所 mark-price 風控語義
2. 清算風險判斷
3. stress / derisk / hard guard

---

## 3. Basis 策略家族（僅保留與第二章相關部分）

Basis 專案可以拆成兩條路線，但目前第二章只聚焦其中一條。

### 3.1 路線 A：有到期日的期貨 Cash-and-Carry

結構：
1. 多現貨
2. 空定期交割合約

優勢來源：
1. 期貨溢價在到期時收斂

重要特性：
1. 具備結構性收斂錨（到期日）
2. 比較接近傳統 carry trade
3. 更適合獨立做另一條研究線

### 3.2 路線 B：Perp Basis Mean Reversion（第二章主軸）

結構：
1. 針對極端 basis 偏離做相對價值交易
2. 核心是 perp 與 spot / index 的價差偏離

優勢來源：
1. basis 的統計回歸
2. 相對價格回落 / 回升

重要特性：
1. 永續沒有到期日
2. 沒有保證收斂
3. 風控品質與成本控制比定期期貨更重要

目前第二章只研究這條：
1. **Perp-Spot / Perp-Index basis 的獨立均值回歸套利**

---

## 4. 資料需求（最小可重複集合）

### 4.1 市場與合約資料

必要欄位：
1. `spot_mid` 或 `spot_bid/spot_ask`
2. `perp_mark`
3. `perp_index`
4. `perp_bid`, `perp_ask`
5. 合約規格：
   - `contract_type`
   - `contract_size`
   - `qty_step`
   - `tick_size`

### 4.2 成本與資本資料

必要欄位 / 輸入：
1. `fee_maker`, `fee_taker`
2. 滑價模型參數
3. `borrow_apr`（若要引入現貨融資成本）
4. 保證金風險代理（例如 `margin_ratio`）

### 4.3 目前階段的研究約束

目前先以你現有模擬資料為主，重點是：
1. 驗證 basis 計算與風控語義
2. 驗證 mark/index 對 risk 的用途
3. 驗證在單一 BTC 標的上，訊號與 execution 是否合理

---

## 5. Basis 單獨訊號建構

採用分層訊號，由簡至進。

### 5.1 核心指標

本研究採用雙層 basis，不再把交易 alpha 與風控 basis 混成同一個 `b_t`。

1. 交易層 basis（alpha）：
`b_t^alpha = basis_trade_t = (perp_mid_t - spot_mid_t) / spot_mid_t`

用途：
1. 進出場判斷
2. 成本後 edge 評估
3. 均值回歸主訊號

2. 風控層 basis（risk）：
`b_t^risk = basis_risk_t = (perp_mark_t - perp_index_t) / perp_index_t`

用途：
1. regime / stress 偵測
2. soft derisk
3. hard guard / kill switch

3. 趨勢：
`EMA_s(b_t^alpha), EMA_m(b_t^alpha), EMA_l(b_t^alpha)`

補充：
1. EMA 主體先以 `basis_trade` 為主，用於判斷偏離是瞬間尖峰還是持續擴大
2. 若後續需要，也可另外對 `b_t^risk` 建立風控專用 EMA，不與 alpha 混用

4. 交易訊號標準化：
`z_t^alpha = (b_t^alpha - rolling_mean_alpha) / rolling_std_alpha`

用途：
1. 作為 Basis Mean Reversion 的主要 entry / exit 訊號
2. 用於 hysteresis 與 cooldown 規則

5. 波動調整風險：
`risk_basis_t = |b_t^risk| / rolling_std_risk`

用途：
1. 衡量風控層 basis 在當前波動背景下有多極端
2. 作為 regime 分類、縮倉與風險懲罰輸入

6. 風控層標準化：
`z_t^risk = (b_t^risk - rolling_mean_risk) / rolling_std_risk`

用途：
1. 輔助判斷 mark-index 偏離是否進入 stress 區
2. 不直接作為交易 alpha 的唯一進出場訊號

### 5.2 制度標籤（只看 basis）

粗分類建議：
1. `calm`：`|z_t^alpha|` 低，且 `risk_basis` / `|z_t^risk|` 低
2. `stretched`：`|z_t^alpha|` 高，但 `risk_basis` 仍在可承受範圍
3. `stress`：`|z_t^risk|` 高，且 `risk_basis` 高，代表 perp 端風控壓力明顯上升

補充原則：
1. `stretched` 可以是交易機會
2. `stress` 不一定是更好的交易機會，很多時候代表應該縮倉或暫停加倉
3. 因此 regime 分類不能只看 `basis_trade`，也要納入 `basis_risk`

用途：
1. 控制 target notional
2. 控制 rebalance 頻率
3. 控制 derisk / hard guard 門檻

---

## 6. 倉位尺寸與 Delta 中性

### 6.1 曝險換算

對於 `linear` 永續：

`coin_exposure = n_contracts * contract_size_coin`

對於 `inverse` 永續：

`coin_exposure = n_contracts * contract_value_usd / perp_mark`

### 6.2 中性目標

給定現貨數量 `q_spot`，目標永續合約數量：

1. `linear`：
`n_target = - q_spot / contract_size_coin`

2. `inverse`：
`n_target = - q_spot * perp_mark / contract_value_usd`

這是 basis 策略的基本中性公式。

目前你的系統若先假設 `linear`，那第二式可先保留為後續擴展。

---

## 7. 再平衡與風控規則（純 basis）

### 7.1 再平衡觸發

以事件為主，不用固定週期硬調整。

主要觸發：
1. `|delta_usd| > delta_threshold`
2. `|z_t^alpha| > z_stop_alpha` 或 `|z_t^risk| > z_stop_risk`
3. 保證金安全門檻觸發
4. `risk_basis` 突變

補充說明：
1. `z_stop_alpha` 代表交易層 basis 偏離已超出一般 MR 可舒適持有區間
2. `z_stop_risk` 代表 mark-index basis 已進入風控 stress 區
3. 前者偏向觸發「停止加倉 / 提高再平衡頻率」，後者偏向觸發「降槓桿 / reduce-only / 退出」
4. 不建議再把兩者混寫成單一 `|z_t| > z_stop`

### 7.2 Hysteresis 與冷卻

為了避免 churn：
1. 區分進場 / 出場門檻
2. `rebalance_cooldown`
3. 最小 notional 變動濾網

### 7.3 保證金優先 Kill Switch

當保證金風險升高時：
1. 優先降低 perp notional
2. 必要時只允許 reduce-only
3. 最後才考慮整體退出

basis 策略沒有到期收斂保證，因此風控一定要比 cash-and-carry 更保守。

---

## 8. 成本感知判斷邏輯

在 basis 單獨模式，決策不應只看價差大小，還要看淨優勢：

`net_edge = expected_reversion_gain - borrow_cost - trading_cost - risk_penalty`

只有當：

`net_edge > edge_threshold`

才值得進場或加倉。

其中 `risk_penalty` 可用：
1. `risk_basis`
2. regime
3. margin 壓力
4. execution friction

這可以避免：
1. 統計上偏離很大
2. 但經濟上其實不划算

---

## 9. 第二章實驗階梯（Basis Standalone）

按序執行，勿跳過。

補充限制：
1. `Stage B0` ~ `Stage B3` 一律先以 `BTCUSDT` 單一標的完成
2. 不在本階段引入多標的輪動、資金分配或 universe selection
3. 多 symbols 屬於後續章節，不納入目前基線驗證

### 9.1 Stage B0：資料與數學驗證

1. 驗證 `basis_trade` 與 `basis_risk` 計算一致性
2. 驗證 linear / inverse 曝險轉換
3. 驗證中性化誤差分布
4. 驗證 mark / index / trade price 的資料語義

成功標準：
1. basis 定義與欄位語義一致
2. 淨曝險漂移可量測且可解釋
3. 不存在資料對齊或公式層級的明顯錯誤

### 9.2 Stage B1：規則化均值回歸基線

1. 固定 z-score 進出場
2. 固定 target notional
3. 加入基本 cooldown + delta threshold 再平衡

紀錄：
1. return / annualized / max drawdown
2. fees / turnover / fills
3. residual / basis contribution

### 9.3 Stage B2：波動 / 風險適應尺寸

1. 依 `risk_basis` 與 regime 調整目標 notional
2. 在 stress regime 收緊倉位

目標：
1. 在接近的回撤下，提升淨收益品質
2. 減少不必要的波動暴露

### 9.4 Stage B3：成本感知門檻

1. 加入 `net_edge > threshold`
2. 與 B2 比較以減少過度交易

目標：
1. 降低 churn
2. 避免在看似極端、實際不划算的 basis 區間進場

---

## 10. 對專案架構的意涵

第二章 basis 要保持與 funding carry 路線分離。

建議：
1. `research/basis_arbitrage/`
   - 只放 basis 的報告、診斷、基線 notebook
2. 策略模組：
   - basis 策略與 funding carry 預設行為分離
3. 評估：
   - KPI schema 可共用，但分析結論分開寫

只有當 Basis Standalone 的 B2 / B3 穩定後，才進入後續章節，研究與 funding carry 的整合。
