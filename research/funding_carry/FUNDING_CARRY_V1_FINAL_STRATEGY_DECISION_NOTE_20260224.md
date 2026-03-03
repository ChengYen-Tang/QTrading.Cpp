# FUNDING_CARRY_V1_FINAL_STRATEGY_DECISION_NOTE_20260224

## 1) 最終定版（V1）

### 1.1 策略核心

- 結構：`Spot Long + Perp Short` 的 delta-neutral carry
- 標的：固定 BTC（`BTCUSDT_SPOT`, `BTCUSDT_PERP`）
- 目標：
  - 以 funding 為主要收益來源
  - 把費用與殘差壓在可控範圍
  - 維持可重現、可解釋、可面試展示

### 1.2 程式碼形態（已收斂）

- 只保留 Funding Carry 策略鏈
- 已移除 Basis 策略模組
- 已移除策略模組內 env sweep 覆寫（避免參數污染與不可追溯）
- 參數來源統一為 config：
  - `research/funding_carry/config/funding_carry_v1.json`

### 1.3 目前啟用的關鍵配置

`funding_carry_v1.json` 目前顯式設定：

- Risk
  - `carry_confidence_boost_enabled = true`
  - `carry_confidence_boost_reference = 0.56`
  - `carry_confidence_boost_max_scale = 0.18`
  - `carry_confidence_boost_power = 1.6`
- Execution
  - `carry_maker_first_enabled = true`
  - `carry_maker_limit_offset_bps = 1.0`
  - `carry_maker_catchup_gap_ratio = 0.55`

其餘參數使用各模組預設值。

---

## 2) 最終版本的實測結果（1e8 資金）

基準 run：
- `D:\QTrading.Cpp\out\build\x64-release\QTrading.Service\logs\20260224_010954`

主要指標：

- Start Equity: `100,000,000.00`
- End Equity: `108,277,580.67`
- Wallet Delta: `+8,277,580.67`
- Total Return: `8.2776%`
- Annualized Return: `2.6496%`
- Funding Sum: `7,563,933.52`
- Total Fees: `110,149.44`
- Residual Price/Basis: `823,796.59`
- `|Residual| / |Funding|`: `0.1089`
- Fill Count: `1,358`
- Max Drawdown: `-0.9368%`

判讀：

- 收益主來源是 funding（符合策略身份）
- 成本相對 funding 可控（fees 占比低）
- residual 不為零，但仍在可接受區（非 pathologic）
- 回撤低，策略形態穩定

---

## 3) 決策過程：我們為什麼最後走到這個思路

下面是「問題 -> 觀察 -> 改法 -> 決策」的收斂路徑。

### 3.1 第一階段：先救出「手續費機器」

問題：
- 初期版本有高 churn（過度頻繁進出/修正），fees 吃掉 carry。

觀察：
- fill_count 高、fees 高、funding 不差但留不住。

改法：
- 降低不必要調倉頻率（cooldown / step / 最小調整量 / 參與率控制）。

決策：
- 先把執行節奏控制住，再談放大收益。

---

### 3.2 第二階段：把策略從「看起來中性」變成「真正中性」

問題：
- 名義上是 delta-neutral，但執行後容易出現淨曝險偏移，造成 residual 擴大。

觀察：
- `net_exposure drift`、`residual` 對 funding 比例偏高。

改法：
- 在 risk/execution 引入更明確的再平衡控制與兩腿對齊邏輯。
- 加入 gross deviation / rebalance threshold 來避免「看似中性、實際偏移」。

決策：
- 優先保證對沖品質，因為這直接影響 residual。

---

### 3.3 第三階段：Spot/Perp 分帳後重構 sizing

問題：
- 分帳後，單看總資金不夠；spot/perp 可用資金與容量限制會讓 target 無法落地。

觀察：
- 有時有目標但下單腿不對稱，或策略停在次優資金部署。

改法：
- risk 端納入 dual-ledger 觀點（spot/perp 各自 capacity）
- 避免只靠靜態 notional 假設

決策：
- 必須讓 sizing 反映資金結構，否則大資金下結果不可信。

---

### 3.4 第四階段：大量實驗後，確認「純調參」邊際遞減

問題：
- 單純改更多門檻/參數，改善幅度越來越小，且常引發副作用（churn、漏吃 funding）。

觀察：
- 一些設定會提高表面收益，但 funding 貢獻比例變差，或 residual 異常上升。

改法：
- 將策略身份檢查列為硬規則：
  - funding 是否為主收益
  - residual/funding 是否在健康區
  - fees 是否可控

決策：
- 不再追求單一數字最大化；改採「收益來源正確 + 風險可控」作為最終標準。

---

### 3.5 第五階段：定版 V1（低自由度、可重現）

最終取捨：

- 保留：
  - carry confidence boost（適度放大高信心時段）
  - maker-first execution（降低交易成本）
- 不保留：
  - 策略模組內 env 臨時覆寫
  - 非 Funding Carry 策略模組
  - 高自由度但難穩定重現的試驗分支

原因：
- 這組在收益、成本、殘差、回撤四維度達到平衡。
- 可直接作為下一階段（P2/P3）的穩定基底。

---

## 4) 為什麼不是其他方向（淘汰理由）

### A. 過硬的負 funding 退出/禁入

- 問題：容易漏掉後續正 funding 區段，且增加切換成本
- 結果：整體績效常不升反降，或 churn 上升

### B. 極端提高槓桿 / notional

- 問題：放大滑價與非理想執行偏差，容易把 residual 放大
- 結果：策略身份變差（收益來源不乾淨）

### C. 大量高自由度參數同時優化

- 問題：可解釋性下降、回放一致性差、過擬合風險高
- 結果：不利於工程維護與面試展示
