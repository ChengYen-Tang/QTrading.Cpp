#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <tuple>
#include <memory_resource>
#include <cstdint>
#include "Dto/Order.hpp"
#include "Dto/Position.hpp"
#include "Dto/Market/Binance/Kline.hpp"
#include "Dto/Account/BalanceSnapshot.hpp"
#include "Dto/Trading/Side.hpp"
#include "Dto/Trading/InstrumentSpec.hpp"
#include "Exchanges/BinanceSimulator/Spot/SpotLedgerEngine.hpp"
#include "Exchanges/BinanceSimulator/Perp/PerpLedgerEngine.hpp"
#include <optional>
#include <functional>

using namespace QTrading::dto;

/// @brief Simulated Binance Futures account supporting one-way and hedge modes.
///        Manages balance, margin, orders, and positions.
class Account {
public:
    struct FundingApplyResult;
    enum class SelfTradePreventionMode {
        None = 0,
        ExpireTaker = 1,
        ExpireMaker = 2,
        ExpireBoth = 3,
    };
    struct OrderRejectInfo {
        enum class Code {
            None = 0,
            InvalidQuantity = 1,
            DuplicateClientOrderId = 2,
            StpExpiredTaker = 3,
            StpExpiredBoth = 4,
            SpotHedgeModeUnsupported = 5,
            SpotInsufficientCash = 6,
            SpotNoInventory = 7,
            SpotQuantityExceedsInventory = 8,
            SpotNoLongPositionToClose = 9,
            HedgeModePositionSideRequired = 10,
            StrictHedgeReduceOnlyDisabled = 11,
            ReduceOnlyNoReduciblePosition = 12,
            PriceFilterBelowMin = 13,
            PriceFilterAboveMax = 14,
            PriceFilterInvalidTick = 15,
            LotSizeBelowMinQty = 16,
            LotSizeAboveMaxQty = 17,
            LotSizeInvalidStep = 18,
            NotionalNoReferencePrice = 19,
            NotionalBelowMin = 20,
            NotionalAboveMax = 21,
            PercentPriceAboveBound = 22,
            PercentPriceBelowBound = 23,
        };

        Code code{ Code::None };
        std::string message;
    };

    class SpotApi {
    public:
        explicit SpotApi(Account& owner);

        QTrading::Dto::Account::BalanceSnapshot get_balance() const;
        double get_cash_balance() const;

        bool place_order(const std::string& symbol,
            double quantity,
            double price,
            QTrading::Dto::Trading::OrderSide side,
            bool reduce_only = false,
            const std::string& client_order_id = {},
            SelfTradePreventionMode stp_mode = SelfTradePreventionMode::None);

        bool place_order(const std::string& symbol,
            double quantity,
            QTrading::Dto::Trading::OrderSide side,
            bool reduce_only = false,
            const std::string& client_order_id = {},
            SelfTradePreventionMode stp_mode = SelfTradePreventionMode::None);

        void close_position(const std::string& symbol, double price = 0.0);
        void cancel_open_orders(const std::string& symbol);

    private:
        Account* owner_{ nullptr };
    };

    class PerpApi {
    public:
        explicit PerpApi(Account& owner);

        QTrading::Dto::Account::BalanceSnapshot get_balance() const;
        double get_wallet_balance() const;
        double get_margin_balance() const;
        double get_available_balance() const;

        bool place_order(const std::string& symbol,
            double quantity,
            double price,
            QTrading::Dto::Trading::OrderSide side,
            QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
            bool reduce_only = false,
            const std::string& client_order_id = {},
            SelfTradePreventionMode stp_mode = SelfTradePreventionMode::None);

        bool place_order(const std::string& symbol,
            double quantity,
            QTrading::Dto::Trading::OrderSide side,
            QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
            bool reduce_only = false,
            const std::string& client_order_id = {},
            SelfTradePreventionMode stp_mode = SelfTradePreventionMode::None);

        void close_position(const std::string& symbol, double price = 0.0);
        void close_position(const std::string& symbol,
            QTrading::Dto::Trading::PositionSide position_side,
            double price = 0.0);
        void cancel_open_orders(const std::string& symbol);
        void set_symbol_leverage(const std::string& symbol, double new_leverage);
        double get_symbol_leverage(const std::string& symbol) const;

        std::vector<FundingApplyResult> apply_funding(
            const std::string& symbol,
            uint64_t funding_time,
            double rate,
            double mark_price);

    private:
        Account* owner_{ nullptr };
    };

    struct AccountInitConfig {
        double spot_initial_cash{ 0.0 };
        double perp_initial_wallet{ 1'000'000.0 };
        int vip_level{ 0 };
        bool strict_binance_mode{ false };
    };

    enum class KlineVolumeSplitMode {
        LegacyTotalOnly = 0,
        TakerBuyOnly = 1,
        TakerBuyOrHeuristic = 2,
    };

    enum class IntraBarPathMode {
        LegacyCloseHeuristic = 0,
        ExpectedPath = 1,
        MonteCarloPath = 2,
    };

    // --- Policies (P2.1) ---
    using FeeRates = std::tuple<double, double>; // (maker, taker)

    struct Policies {
        // Perp fee rates by VIP level (maker, taker).
        std::function<FeeRates(int vip_level)> fee_rates;
        // Optional spot fee rates by VIP level (maker, taker).
        // If not provided, spot falls back to the default spot fee table.
        std::function<FeeRates(int vip_level)> spot_fee_rates;

        // Decide if order can fill on this kline and whether it's taker
        std::function<std::pair<bool, bool>(const Order& ord, const QTrading::Dto::Market::Binance::KlineDto& k)> can_fill_and_taker;

        // Directional liquidity split: returns {has_dir_liq, {buy_liq, sell_liq}}
        std::function<std::pair<bool, std::pair<double, double>>(
            KlineVolumeSplitMode mode,
            const QTrading::Dto::Market::Binance::KlineDto& k)> directional_liquidity;

        // Execution price given slippage settings
        std::function<double(const Order& ord,
            const QTrading::Dto::Market::Binance::KlineDto& k,
            double market_exec_slip,
            double limit_exec_slip)> execution_price;

        // Liquidation execution price (defaults to Low/High)
        std::function<double(const Position& pos, const QTrading::Dto::Market::Binance::KlineDto& k)> liquidation_price;
    };

    static Policies DefaultPolicies();

    Account(double initial_balance, int vip_level = 0);
    Account(double initial_balance, int vip_level, Policies policies);
    explicit Account(const AccountInitConfig& init_config);
    Account(const AccountInitConfig& init_config, Policies policies);

    SpotApi spot;
    PerpApi perp;

    QTrading::Dto::Account::BalanceSnapshot get_balance() const;
    QTrading::Dto::Account::BalanceSnapshot get_perp_balance() const;
    QTrading::Dto::Account::BalanceSnapshot get_spot_balance() const;

    double total_unrealized_pnl() const;
    double get_equity() const;

    double get_wallet_balance() const;
    double get_spot_cash_balance() const;
    double get_total_cash_balance() const;
    double get_margin_balance() const;
    double get_available_balance() const;
    bool transfer_spot_to_perp(double amount);
    bool transfer_perp_to_spot(double amount);

    void set_position_mode(bool hedgeMode);
    bool is_hedge_mode() const;
    void set_strict_binance_mode(bool enable);
    bool is_strict_binance_mode() const;

    // Legacy top-level APIs kept for compatibility.
    // New call sites should prefer `account.perp.set_symbol_leverage(...)`
    // and `account.perp.get_symbol_leverage(...)`.
    void set_symbol_leverage(const std::string& symbol, double newLeverage);
    double get_symbol_leverage(const std::string& symbol) const;
    void set_instrument_type(const std::string& symbol, QTrading::Dto::Trading::InstrumentType type);
    void set_instrument_spec(const std::string& symbol, const QTrading::Dto::Trading::InstrumentSpec& spec);
    QTrading::Dto::Trading::InstrumentSpec get_instrument_spec(const std::string& symbol) const;

    // Legacy top-level order APIs kept for compatibility.
    // New call sites should prefer `account.spot.*` / `account.perp.*`.
    bool place_order(const std::string& symbol,
        double quantity,
        double price,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
        bool reduce_only = false,
        const std::string& client_order_id = {},
        SelfTradePreventionMode stp_mode = SelfTradePreventionMode::None);

    bool place_order(const std::string& symbol,
        double quantity,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both,
        bool reduce_only = false,
        const std::string& client_order_id = {},
        SelfTradePreventionMode stp_mode = SelfTradePreventionMode::None);
    std::optional<OrderRejectInfo> consume_last_order_reject_info();

    void update_positions(const std::unordered_map<std::string, std::pair<double, double>>& symbol_price_volume);
    void update_positions(const std::unordered_map<std::string, QTrading::Dto::Market::Binance::KlineDto>& symbol_kline);

    struct FundingApplyResult {
        int position_id{};
        bool is_long{};
        double quantity{};
        double funding{};
    };

    /// @brief Apply funding to all positions for the given symbol.
    /// @param symbol Trading symbol.
    /// @param funding_time Funding timestamp (ms since epoch).
    /// @param rate Funding rate.
    /// @param mark_price Mark price used to compute notional.
    std::vector<FundingApplyResult> apply_funding(const std::string& symbol, uint64_t funding_time, double rate, double mark_price);

    // Legacy top-level close/cancel APIs kept for compatibility.
    // New call sites should prefer `account.spot.*` / `account.perp.*`.
    void close_position(const std::string& symbol, double price);
    void close_position(const std::string& symbol);

    void close_position(const std::string& symbol,
        QTrading::Dto::Trading::PositionSide position_side,
        double price = 0.0);

    void cancel_order_by_id(int order_id);
    void cancel_open_orders(const std::string& symbol);

    const std::vector<Order>& get_all_open_orders() const;
    const std::vector<Position>& get_all_positions() const;

    void set_market_slippage_buffer(double pct);

    // Kline-based market execution slippage (fraction of price, e.g. 0.001 = 0.1%).
    // Applied to market fills in update_positions.
    void set_market_execution_slippage(double pct);

    // Kline-based limit execution slippage (fraction of price).
    // Applied to triggered limit fills to model worse price within the candle while preserving limit protection.
    void set_limit_execution_slippage(double pct);

    void set_kline_volume_split_mode(KlineVolumeSplitMode mode);
    void set_intra_bar_path_mode(IntraBarPathMode mode);
    IntraBarPathMode intra_bar_path_mode() const;
    void set_intra_bar_monte_carlo_samples(size_t samples);
    size_t intra_bar_monte_carlo_samples() const;
    void set_intra_bar_random_seed(uint64_t seed);
    uint64_t intra_bar_random_seed() const;
    void set_limit_fill_probability_enabled(bool enable);
    bool is_limit_fill_probability_enabled() const;
    void set_limit_fill_probability_coefficients(double intercept,
        double penetration_weight,
        double size_ratio_weight,
        double taker_flow_weight,
        double volatility_weight);
    void set_market_impact_slippage_enabled(bool enable);
    bool is_market_impact_slippage_enabled() const;
    void set_market_impact_slippage_params(double b0_bps,
        double b1_bps,
        double beta,
        double b2_bps,
        double b3_bps);
    void set_taker_probability_model_enabled(bool enable);
    bool is_taker_probability_model_enabled() const;
    void set_taker_probability_model_coefficients(double intercept,
        double same_side_flow_weight,
        double size_ratio_weight,
        double volatility_weight,
        double penetration_weight);

    void set_enable_console_output(bool enable);
    bool is_console_output_enabled() const;

    // Limit the number of open orders checked per symbol per tick (0 = no limit).
    void set_max_match_orders_per_symbol(size_t limit);
    size_t max_match_orders_per_symbol() const;

    struct FillEvent {
        int order_id{};
        std::string symbol;
        QTrading::Dto::Trading::OrderSide side{};
        QTrading::Dto::Trading::PositionSide position_side{};
        bool reduce_only{};
        double order_qty{};
        double order_price{};
        double exec_qty{};
        double exec_price{};
        double remaining_qty{};
        bool is_taker{};
        double taker_probability{ 0.0 };
        double fill_probability{ 1.0 };
        double impact_slippage_bps{ 0.0 };
        double fee{};
        double fee_rate{};
        int closing_position_id{};
        QTrading::Dto::Trading::InstrumentType instrument_type{ QTrading::Dto::Trading::InstrumentType::Perp };
        QTrading::Dto::Account::BalanceSnapshot balance_snapshot{};
        QTrading::Dto::Account::BalanceSnapshot perp_balance_snapshot{};
        QTrading::Dto::Account::BalanceSnapshot spot_balance_snapshot{};
        double total_cash_balance_snapshot{};
        std::vector<Position> positions_snapshot{};
    };

    std::vector<FillEvent> drain_fill_events();

private:
    SpotLedgerEngine spot_ledger_{};
    PerpLedgerEngine perp_ledger_{};

    int vip_level_;
    bool hedge_mode_;
    bool strict_binance_mode_;

    std::unordered_map<std::string, double> symbol_leverage_;

    int next_order_id_;
    int next_position_id_;

    std::vector<Order> open_orders_;
    std::vector<Position> positions_;

    std::unordered_map<int, int> order_to_position_;

    // Fast lookup indices (rebuilt when containers are rebuilt).
    std::unordered_map<int, size_t> open_order_index_by_id_;
    std::unordered_map<int, size_t> position_index_by_id_;
    std::unordered_map<std::string, std::vector<size_t>> position_indices_by_symbol_;

    // Reusable buffers/caches to reduce allocations in update_positions().
    std::pmr::unsynchronized_pool_resource tick_memory_;
    std::unordered_map<std::string, size_t> symbol_id_by_name_;
    std::vector<double> remaining_vol_;
    std::vector<std::pair<double, double>> remaining_liq_;
    std::vector<char> has_dir_liq_;
    // Cached per-symbol open order indices in priority order (indexed by symbol id).
    std::vector<std::vector<size_t>> per_symbol_;
    std::vector<size_t> per_symbol_active_ids_;
    std::vector<const QTrading::Dto::Market::Binance::KlineDto*> kline_by_id_;
    struct FillCandidate {
        size_t idx{};
        bool is_taker{};
    };
    std::vector<size_t> merge_indices_;
    std::vector<Position> merged_positions_;

    // Last known mark/close price per symbol id (from kline ClosePrice).
    // Used for market-order notional estimation.
    std::vector<double> last_mark_price_by_id_;

    // For market orders, notional is estimated as qty * mark * (1 + buffer).
    double market_slippage_buffer_{ 0.005 };

    // For market orders, fill price is pessimistically adjusted using kline OHLC.
    // buy: min(High, Close*(1+slip)), sell: max(Low, Close*(1-slip))
    double market_execution_slippage_{ 0.0 };

    // For limit orders that trigger, optionally fill at a worse price within OHLC.
    // buy limit: fill = min(limit, close*(1+slip), high)
    // sell limit: fill = max(limit, close*(1-slip), low)
    double limit_execution_slippage_{ 0.0 };

    Policies policies_{ };

    KlineVolumeSplitMode kline_volume_split_mode_{ KlineVolumeSplitMode::TakerBuyOnly };
    IntraBarPathMode intra_bar_path_mode_{ IntraBarPathMode::LegacyCloseHeuristic };
    size_t intra_bar_monte_carlo_samples_{ 64 };
    uint64_t intra_bar_random_seed_{ 0x9E3779B97F4A7C15ull };

    bool enable_console_output_{ false };
    size_t max_match_orders_per_symbol_{ 256 };
    bool limit_fill_probability_enabled_{ false };
    double fill_prob_intercept_{ 1.0 };
    double fill_prob_penetration_weight_{ 2.0 };
    double fill_prob_size_ratio_weight_{ 2.0 };
    double fill_prob_taker_flow_weight_{ 1.0 };
    double fill_prob_volatility_weight_{ 0.5 };
    bool market_impact_slippage_enabled_{ false };
    double impact_b0_bps_{ 0.0 };
    double impact_b1_bps_{ 8.0 };
    double impact_beta_{ 0.7 };
    double impact_b2_bps_{ 20.0 };
    double impact_b3_bps_{ 15.0 };
    bool taker_probability_model_enabled_{ false };
    double taker_prob_intercept_{ -2.0 };
    double taker_prob_same_side_flow_weight_{ 2.0 };
    double taker_prob_size_ratio_weight_{ 1.5 };
    double taker_prob_volatility_weight_{ 1.0 };
    double taker_prob_penetration_weight_{ 1.0 };

    // Monotonic state version for O(1) change detection by exchange.
    uint64_t state_version_{ 0 };
    // Monotonic open-order version for per-symbol cache invalidation.
    uint64_t open_orders_version_{ 0 };
    uint64_t per_symbol_cache_version_{ static_cast<uint64_t>(-1) };
    QTrading::Dto::Trading::InstrumentRegistry instrument_registry_{};
    // Cached balance snapshot to avoid repeated O(P+O) scans.
    mutable QTrading::Dto::Account::BalanceSnapshot balance_cache_{};
    mutable uint64_t balance_cache_version_{ static_cast<uint64_t>(-1) };
    uint64_t balance_version_{ 0 };
    std::vector<FillEvent> fill_events_{};
    std::optional<OrderRejectInfo> last_order_reject_info_{};

    int generate_order_id();
    int generate_position_id();

    std::tuple<double, double> get_tier_info(double notional) const;
    double maintenance_margin_for_notional_(double notional) const;
    std::tuple<double, double> get_fee_rates(QTrading::Dto::Trading::InstrumentType instrument_type) const;
    bool adjust_position_leverage(const std::string& symbol, double oldLev, double newLev);

    void place_closing_order(int position_id, double quantity, double price);

    void merge_positions();

    bool handleOneWayReverseOrder(const std::string& symbol,
        double quantity,
        double price,
        QTrading::Dto::Trading::OrderSide side,
        const std::string& client_order_id,
        SelfTradePreventionMode stp_mode);
    bool place_spot_order(const std::string& symbol,
        double quantity,
        double price,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side,
        bool reduce_only,
        const std::string& client_order_id,
        SelfTradePreventionMode stp_mode,
        const QTrading::Dto::Trading::InstrumentSpec& instrument_spec);
    bool place_perp_order(const std::string& symbol,
        double quantity,
        double price,
        QTrading::Dto::Trading::OrderSide side,
        QTrading::Dto::Trading::PositionSide position_side,
        bool reduce_only,
        const std::string& client_order_id,
        SelfTradePreventionMode stp_mode,
        const QTrading::Dto::Trading::InstrumentSpec& instrument_spec);
    bool validate_order_filters_(const std::string& symbol,
        double quantity,
        double price,
        QTrading::Dto::Trading::OrderSide side,
        const QTrading::Dto::Trading::InstrumentSpec& instrument_spec);
    bool has_reducible_position_for_order_(const Order& ord) const;
    void update_unrealized_for_symbol_(const std::string& symbol, double close_price);
    bool has_open_perp_position_() const;
    void apply_perp_liquidation_(double taker_fee, bool& open_orders_changed, bool& positions_changed);
    void process_open_orders_pipeline_(bool& dirty, bool& open_orders_changed, bool& positions_changed);
    std::pair<bool, bool> evaluate_can_fill_and_taker_(const Order& ord,
        const QTrading::Dto::Market::Binance::KlineDto& k) const;
    double limit_fill_probability_(const Order& ord,
        const QTrading::Dto::Market::Binance::KlineDto& k,
        bool is_taker) const;
    std::pair<double, double> apply_market_impact_slippage_(const Order& ord,
        const QTrading::Dto::Market::Binance::KlineDto& k,
        double base_fill_price,
        double fill_qty) const;
    double taker_probability_(const Order& ord,
        const QTrading::Dto::Market::Binance::KlineDto& k,
        bool base_is_taker) const;
    void close_spot_position_(const std::string& symbol, double price);
    void close_perp_position_(const std::string& symbol, double price);
    void close_perp_position_side_(const std::string& symbol, QTrading::Dto::Trading::PositionSide position_side, double price);
    bool cancel_spot_open_orders_(const std::string& symbol);
    bool cancel_perp_open_orders_(const std::string& symbol);

    void processClosingOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover);

    bool processReduceOnlyOrder(Order& ord, double fill_qty, double fill_price, double fee, std::vector<Order>& leftover);

    void processNormalOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
        double fee, double feeRate, std::vector<Order>& leftover);

    bool processSpotOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
        double fee, double feeRate, std::vector<Order>& leftover);
    bool processPerpOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
        double fee, double feeRate, std::vector<Order>& leftover);
    void applyOpeningFillToPosition(Order& ord, double fill_qty, double fill_price, double notional,
        double init_margin, double maint_margin, double fee, double lev, double feeRate, bool is_spot_symbol);
    void applySpotClosingCashflow(double close_qty, double fill_price, double fee, double& freed_margin, double& freed_maint);
    void applyPerpClosingCashflow(double realized_pnl, double fee, double freed_margin);

    void processOpeningOrder(Order& ord, double fill_qty, double fill_price, double notional,
        double fee, double feeRate, std::vector<Order>& leftover);

    void rebuild_open_order_index_();
    void rebuild_position_index_();
    void rebuild_per_symbol_cache_();
    size_t get_symbol_id_(const std::string& symbol);
    void ensure_symbol_capacity_(size_t id);
    void mark_open_orders_dirty_();
    void mark_balance_dirty_();
    void clear_last_order_reject_info_();
    bool reject_order_(OrderRejectInfo::Code code, std::string message);
    bool has_open_order_with_client_id_(const std::string& client_order_id) const;
    size_t count_stp_conflicting_orders_(const std::string& symbol,
        QTrading::Dto::Trading::OrderSide incoming_side,
        double incoming_price,
        QTrading::Dto::Trading::InstrumentType instrument_type) const;
    size_t cancel_stp_conflicting_orders_(const std::string& symbol,
        QTrading::Dto::Trading::OrderSide incoming_side,
        double incoming_price,
        QTrading::Dto::Trading::InstrumentType instrument_type);
    const QTrading::Dto::Trading::InstrumentSpec& resolve_instrument_spec_(const std::string& symbol) const;

public:
    uint64_t get_state_version() const noexcept { return state_version_; }
};
