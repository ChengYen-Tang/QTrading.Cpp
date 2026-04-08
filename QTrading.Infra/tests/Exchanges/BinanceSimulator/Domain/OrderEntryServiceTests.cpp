#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>

#include "Data/Binance/MarketData.hpp"
#include "Dto/Trading/InstrumentSpec.hpp"
#include "Dto/Market/Binance/MultiKline.hpp"
#include "Exchanges/BinanceSimulator/Domain/FillSettlementEngine.hpp"
#include "Exchanges/BinanceSimulator/Domain/MatchingEngine.hpp"
#include "Exchanges/BinanceSimulator/Account/Account.hpp"
#include "Exchanges/BinanceSimulator/Contracts/OrderCommandRequest.hpp"
#include "Exchanges/BinanceSimulator/Domain/OrderEntryService.hpp"
#include "Exchanges/BinanceSimulator/State/BinanceExchangeRuntimeState.hpp"
#include "Exchanges/BinanceSimulator/State/StepKernelState.hpp"

using QTrading::Infra::Exchanges::BinanceSim::Account;
using QTrading::Infra::Exchanges::BinanceSim::Contracts::OrderCommandKind;
using QTrading::Infra::Exchanges::BinanceSim::Contracts::OrderCommandRequest;
using QTrading::Infra::Exchanges::BinanceSim::Contracts::OrderRejectInfo;
using QTrading::Infra::Exchanges::BinanceSim::Domain::OrderEntryService;
using QTrading::Infra::Exchanges::BinanceSim::State::BinanceExchangeRuntimeState;
using QTrading::Infra::Exchanges::BinanceSim::State::StepKernelState;

namespace {

Account::AccountInitConfig MakeLegacyCtorInitConfig(double init_balance, int vip_level = 0)
{
    Account::AccountInitConfig cfg{};
    cfg.init_balance = init_balance;
    cfg.spot_initial_cash = 0.0;
    cfg.perp_initial_wallet = init_balance;
    cfg.vip_level = vip_level;
    return cfg;
}

StepKernelState make_step_state_with_perp_symbol()
{
    StepKernelState state{};
    state.symbols.push_back("BTCUSDT");
    state.symbol_to_id.emplace("BTCUSDT", 0);
    state.symbol_instrument_type_by_id.push_back(QTrading::Dto::Trading::InstrumentType::Perp);
    state.symbol_spec_by_id.push_back(QTrading::Dto::Trading::PerpInstrumentSpec());
    state.symbol_maintenance_margin_tiers_by_id.emplace_back();
    return state;
}

StepKernelState make_step_state_with_symbols(std::initializer_list<std::string> symbols)
{
    StepKernelState state{};
    size_t symbol_id = 0;
    for (const auto& symbol : symbols) {
        state.symbols.push_back(symbol);
        state.symbol_to_id.emplace(symbol, symbol_id++);
        state.symbol_instrument_type_by_id.push_back(QTrading::Dto::Trading::InstrumentType::Perp);
        state.symbol_spec_by_id.push_back(QTrading::Dto::Trading::PerpInstrumentSpec());
        state.symbol_maintenance_margin_tiers_by_id.emplace_back();
    }
    return state;
}

OrderCommandRequest make_perp_limit_request(double quantity, double price)
{
    OrderCommandRequest request{};
    request.kind = OrderCommandKind::PerpLimit;
    request.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    request.symbol = "BTCUSDT";
    request.quantity = quantity;
    request.price = price;
    request.side = QTrading::Dto::Trading::OrderSide::Buy;
    request.position_side = QTrading::Dto::Trading::PositionSide::Both;
    return request;
}

OrderCommandRequest make_perp_market_request(double quantity)
{
    OrderCommandRequest request = make_perp_limit_request(quantity, 0.0);
    request.kind = OrderCommandKind::PerpMarket;
    return request;
}

OrderCommandRequest make_perp_close_position_request(
    QTrading::Dto::Trading::OrderSide side = QTrading::Dto::Trading::OrderSide::Sell,
    QTrading::Dto::Trading::PositionSide position_side = QTrading::Dto::Trading::PositionSide::Both)
{
    OrderCommandRequest request{};
    request.kind = OrderCommandKind::PerpClosePosition;
    request.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    request.symbol = "BTCUSDT";
    request.quantity = 0.0;
    request.price = 0.0;
    request.side = side;
    request.position_side = position_side;
    request.reduce_only = false;
    request.close_position = true;
    return request;
}

QTrading::Dto::Market::Binance::MultiKlineDto make_single_symbol_market(
    const std::string& symbol,
    double open_price,
    double high_price,
    double low_price,
    double close_price,
    double volume,
    uint64_t timestamp)
{
    QTrading::Dto::Market::Binance::MultiKlineDto market{};
    market.Timestamp = timestamp;
    market.symbols = std::make_shared<std::vector<std::string>>(std::initializer_list<std::string>{ symbol });
    market.trade_klines_by_id.resize(1);
    market.trade_klines_by_id[0] = QTrading::Dto::Market::Binance::TradeKlineDto{
        timestamp,
        open_price,
        high_price,
        low_price,
        close_price,
        volume,
        timestamp + 60000,
        close_price * volume,
        1,
        0.0,
        0.0 };
    market.mark_klines_by_id.resize(1);
    market.index_klines_by_id.resize(1);
    market.funding_by_id.resize(1);
    return market;
}

std::filesystem::path write_single_kline_csv(const std::string& file_name, double close_price)
{
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / (file_name + "_" + unique + ".csv");
    std::ofstream file(path, std::ios::trunc);
    file << "openTime,open,high,low,close,volume,closeTime,quoteVol,tradeCnt,takerBB,takerBQ\n";
    file << "0," << close_price << ',' << close_price << ',' << close_price << ',' << close_price
         << ",1000,60000,100,1,0,0\n";
    return path;
}

std::filesystem::path write_single_kline_csv(
    const std::string& file_name,
    double open_price,
    double high_price,
    double low_price,
    double close_price,
    double volume)
{
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / (file_name + "_" + unique + ".csv");
    std::ofstream file(path, std::ios::trunc);
    file << "openTime,open,high,low,close,volume,closeTime,quoteVol,tradeCnt,takerBB,takerBQ\n";
    file << "0," << open_price << ',' << high_price << ',' << low_price << ',' << close_price
         << ',' << volume << ",60000," << (close_price * volume) << ",1,0,0\n";
    return path;
}

QTrading::dto::Position make_perp_position(
    const std::string& symbol,
    bool is_long,
    double quantity = 1.0,
    double entry_price = 100.0)
{
    QTrading::dto::Position position{};
    position.id = 1;
    position.order_id = 1;
    position.symbol = symbol;
    position.quantity = quantity;
    position.entry_price = entry_price;
    position.is_long = is_long;
    position.unrealized_pnl = 0.0;
    position.notional = quantity * entry_price;
    position.initial_margin = 10.0;
    position.maintenance_margin = 0.0;
    position.fee = 0.0;
    position.leverage = 10.0;
    position.fee_rate = 0.001;
    position.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    return position;
}

QTrading::dto::Position make_spot_position(
    const std::string& symbol,
    double quantity,
    double entry_price)
{
    QTrading::dto::Position position{};
    position.id = 1;
    position.order_id = 1;
    position.symbol = symbol;
    position.quantity = quantity;
    position.entry_price = entry_price;
    position.is_long = true;
    position.notional = quantity * entry_price;
    position.instrument_type = QTrading::Dto::Trading::InstrumentType::Spot;
    return position;
}

} // namespace

TEST(OrderEntryServiceTest, InstrumentFiltersRejectInvalidPriceTickAndRange)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.min_price = 100.0;
    spec.max_price = 200.0;
    spec.price_tick_size = 0.1;
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    EXPECT_FALSE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 99.9),
        reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::PriceFilterBelowMin);

    EXPECT_FALSE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 100.05),
        reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::PriceFilterInvalidTick);

    EXPECT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 100.1),
        reject));
    EXPECT_FALSE(reject.has_value());
}

TEST(OrderEntryServiceTest, InstrumentFiltersRejectInvalidQuantityStepAndBounds)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.min_qty = 0.1;
    spec.max_qty = 5.0;
    spec.qty_step_size = 0.1;
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    EXPECT_FALSE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(0.05, 100.0),
        reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::LotSizeBelowMinQty);

    EXPECT_FALSE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(0.15, 100.0),
        reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::LotSizeInvalidStep);

    EXPECT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(0.2, 100.0),
        reject));
    EXPECT_FALSE(reject.has_value());
}

TEST(OrderEntryServiceTest, InstrumentFiltersRejectOrderByMinNotional)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.min_notional = 50.0;
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    EXPECT_FALSE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(0.4, 100.0),
        reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::NotionalBelowMin);

    EXPECT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(0.5, 100.0),
        reject));
    EXPECT_FALSE(reject.has_value());
}

TEST(OrderEntryServiceTest, InstrumentFiltersPerpMarketNotionalUsesFirstBarMarkOpenPrice)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.min_notional = 150.0;
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    const auto trade_csv = write_single_kline_csv(
        "order_entry_trade_mark_open",
        100.0,
        100.0,
        100.0,
        100.0,
        10.0);
    const auto mark_csv = write_single_kline_csv(
        "order_entry_mark_open",
        100.0,
        200.0,
        100.0,
        200.0,
        10.0);
    step_state.market_data.emplace_back("BTCUSDT", trade_csv.string());
    step_state.mark_data_pool.emplace_back("BTCUSDT", mark_csv.string());
    step_state.mark_data_id_by_symbol.push_back(0);
    step_state.mark_cursor_by_symbol.push_back(0);
    step_state.replay_cursor.push_back(0);

    EXPECT_FALSE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_market_request(1.0),
        reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::NotionalBelowMin);

    std::filesystem::remove(trade_csv);
    std::filesystem::remove(mark_csv);
}

TEST(OrderEntryServiceTest, InstrumentFiltersPerpMarketNotionalUsesMarkPrice)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.min_notional = 150.0;
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    const auto trade_csv = write_single_kline_csv("order_entry_trade", 100.0);
    const auto mark_csv = write_single_kline_csv("order_entry_mark", 200.0);
    step_state.market_data.emplace_back("BTCUSDT", trade_csv.string());
    step_state.mark_data_pool.emplace_back("BTCUSDT", mark_csv.string());
    step_state.mark_data_id_by_symbol.push_back(0);
    step_state.mark_cursor_by_symbol.push_back(0);
    step_state.replay_cursor.push_back(0);

    EXPECT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_market_request(1.0),
        reject));
    EXPECT_FALSE(reject.has_value());

    std::filesystem::remove(trade_csv);
    std::filesystem::remove(mark_csv);
}

TEST(OrderEntryServiceTest, InstrumentFiltersPercentPriceRejectsOutsideBounds)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.percent_price_by_side = false;
    spec.percent_price_multiplier_up = 1.05;
    spec.percent_price_multiplier_down = 0.95;
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    const auto trade_csv = write_single_kline_csv("order_entry_percent_price", 100.0);
    step_state.market_data.emplace_back("BTCUSDT", trade_csv.string());
    step_state.replay_cursor.push_back(0);

    EXPECT_FALSE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 106.0),
        reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::PercentPriceAboveBound);

    EXPECT_FALSE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 94.0),
        reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::PercentPriceBelowBound);

    EXPECT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 100.0),
        reject));
    EXPECT_FALSE(reject.has_value());

    std::filesystem::remove(trade_csv);
}

TEST(OrderEntryServiceTest, InstrumentFiltersPercentPriceUsesFirstBarTradeOpenPrice)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.percent_price_by_side = false;
    spec.percent_price_multiplier_up = 1.05;
    spec.percent_price_multiplier_down = 0.95;
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    const auto trade_csv = write_single_kline_csv(
        "order_entry_percent_price_open",
        100.0,
        200.0,
        100.0,
        200.0,
        10.0);
    step_state.market_data.emplace_back("BTCUSDT", trade_csv.string());
    step_state.replay_cursor.push_back(0);

    EXPECT_FALSE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(150.0 / 100.0, 106.0),
        reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::PercentPriceAboveBound);

    std::filesystem::remove(trade_csv);
}

TEST(OrderEntryServiceTest, InstrumentFiltersPercentPriceBySideUsesBidAskBounds)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.percent_price_by_side = true;
    spec.bid_multiplier_up = 1.05;
    spec.bid_multiplier_down = 0.95;
    spec.ask_multiplier_up = 1.02;
    spec.ask_multiplier_down = 0.98;
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    const auto trade_csv = write_single_kline_csv("order_entry_percent_price_by_side", 100.0);
    step_state.market_data.emplace_back("BTCUSDT", trade_csv.string());
    step_state.replay_cursor.push_back(0);

    auto buy_request = make_perp_limit_request(1.0, 104.0);
    buy_request.side = QTrading::Dto::Trading::OrderSide::Buy;
    EXPECT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        buy_request,
        reject));
    EXPECT_FALSE(reject.has_value());

    auto sell_request = make_perp_limit_request(1.0, 104.0);
    sell_request.side = QTrading::Dto::Trading::OrderSide::Sell;
    EXPECT_FALSE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        sell_request,
        reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::PercentPriceAboveBound);

    std::filesystem::remove(trade_csv);
}

TEST(OrderEntryServiceTest, PerpMarketTakeBoundRejectsMarketOrderWhenTradeMarkGapTooLarge)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.market_take_bound = 0.01; // 1%
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    runtime_state.last_status_snapshot.prices.push_back(
        QTrading::Infra::Exchanges::BinanceSim::Contracts::StatusPriceSnapshot{
            "BTCUSDT", 103.0, true, 103.0, true, 100.0, true, 0, 100.0, true, 0 });

    EXPECT_FALSE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_market_request(1.0),
        reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::MarketTakeBoundExceeded);
}

TEST(OrderEntryServiceTest, PerpMarketTakeBoundAcceptsMarketOrderWithinBound)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.market_take_bound = 0.02; // 2%
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    runtime_state.last_status_snapshot.prices.push_back(
        QTrading::Infra::Exchanges::BinanceSim::Contracts::StatusPriceSnapshot{
            "BTCUSDT", 101.0, true, 101.0, true, 100.0, true, 0, 100.0, true, 0 });

    EXPECT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_market_request(1.0),
        reject));
    EXPECT_FALSE(reject.has_value());
}

TEST(OrderEntryServiceTest, PerpTriggerProtectRejectsMarketOrderWhenMarkIndexGapTooLarge)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.trigger_protect = 0.03; // 3%
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    runtime_state.last_status_snapshot.prices.push_back(
        QTrading::Infra::Exchanges::BinanceSim::Contracts::StatusPriceSnapshot{
            "BTCUSDT", 102.0, true, 102.0, true, 105.0, true, 0, 100.0, true, 0 });

    EXPECT_FALSE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_market_request(1.0),
        reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::TriggerProtectExceeded);
}

TEST(OrderEntryServiceTest, PerpTriggerProtectUsesFirstBarMarkAndIndexOpenPrices)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.trigger_protect = 0.03; // 3%
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    const auto trade_csv = write_single_kline_csv(
        "order_entry_trigger_trade_open",
        100.0,
        100.0,
        100.0,
        100.0,
        10.0);
    const auto mark_csv = write_single_kline_csv(
        "order_entry_trigger_mark_open",
        100.0,
        105.0,
        100.0,
        105.0,
        10.0);
    const auto index_csv = write_single_kline_csv(
        "order_entry_trigger_index_open",
        100.0,
        100.0,
        100.0,
        100.0,
        10.0);
    step_state.market_data.emplace_back("BTCUSDT", trade_csv.string());
    step_state.mark_data_pool.emplace_back("BTCUSDT", mark_csv.string());
    step_state.index_data_pool.emplace_back("BTCUSDT", index_csv.string());
    step_state.mark_data_id_by_symbol.push_back(0);
    step_state.index_data_id_by_symbol.push_back(0);
    step_state.mark_cursor_by_symbol.push_back(0);
    step_state.index_cursor_by_symbol.push_back(0);
    step_state.replay_cursor.push_back(0);

    EXPECT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_market_request(1.0),
        reject));
    EXPECT_FALSE(reject.has_value());

    std::filesystem::remove(trade_csv);
    std::filesystem::remove(mark_csv);
    std::filesystem::remove(index_csv);
}

TEST(OrderEntryServiceTest, ImmediatelyExecutableLimitIsTakerFee)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 2000.0),
        reject));
    EXPECT_FALSE(reject.has_value());

    const auto market = make_single_symbol_market("BTCUSDT", 1000.0, 1000.0, 1000.0, 1000.0, 10.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_NEAR(runtime_state.positions[0].fee_rate, 0.0005, 1e-12);
}

TEST(OrderEntryServiceTest, LimitOrderFillsAtLimitPrice)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 2000.0),
        reject));
    EXPECT_FALSE(reject.has_value());

    const auto market = make_single_symbol_market("BTCUSDT", 1000.0, 1000.0, 1000.0, 1000.0, 10.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_NEAR(runtime_state.positions[0].entry_price, 2000.0, 1e-12);
}

TEST(OrderEntryServiceTest, PerpFillUpdatesLeverageAndMarginFieldsFromSymbolLeverage)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};
    runtime_state.symbol_leverage["BTCUSDT"] = 20.0;

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(2.0, 100.0),
        reject));
    EXPECT_FALSE(reject.has_value());

    const auto market = make_single_symbol_market("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 10.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    ASSERT_EQ(runtime_state.positions.size(), 1u);
    const auto& opened = runtime_state.positions[0];
    EXPECT_NEAR(opened.leverage, 20.0, 1e-12);
    EXPECT_NEAR(opened.initial_margin, 10.0, 1e-12);
    EXPECT_NEAR(opened.maintenance_margin, 0.8, 1e-12);

    runtime_state.symbol_leverage["BTCUSDT"] = 10.0;
    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 100.0),
        reject));
    EXPECT_FALSE(reject.has_value());
    fills.clear();
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    ASSERT_EQ(runtime_state.positions.size(), 1u);
    const auto& merged = runtime_state.positions[0];
    EXPECT_NEAR(merged.quantity, 3.0, 1e-12);
    EXPECT_NEAR(merged.leverage, 10.0, 1e-12);
    EXPECT_NEAR(merged.initial_margin, 30.0, 1e-12);
    EXPECT_NEAR(merged.maintenance_margin, 1.2, 1e-12);
}

TEST(OrderEntryServiceTest, PerpFillMaintenanceMarginUsesTieredBracketsAboveFirstTier)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(2'000'000.0));
    std::optional<OrderRejectInfo> reject{};
    runtime_state.symbol_leverage["BTCUSDT"] = 125.0;

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 100000.0),
        reject));
    EXPECT_FALSE(reject.has_value());

    const auto market = make_single_symbol_market("BTCUSDT", 100000.0, 100000.0, 100000.0, 100000.0, 10.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_NEAR(runtime_state.positions[0].notional, 100000.0, 1e-9);
    EXPECT_NEAR(runtime_state.positions[0].maintenance_margin, 450.0, 1e-9);
}

TEST(OrderEntryServiceTest, PerpFillMaintenanceMarginUsesSymbolSpecificTiersWhenProvided)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};
    runtime_state.symbol_leverage["BTCUSDT"] = 20.0;

    step_state.symbol_maintenance_margin_tiers_by_id[0] = {
        { 1000.0, 0.0100, 125.0 },
        { std::numeric_limits<double>::max(), 0.0200, 100.0 }
    };

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(2.0, 100.0),
        reject));
    EXPECT_FALSE(reject.has_value());

    const auto market = make_single_symbol_market("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 10.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        step_state,
        fills);

    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_NEAR(runtime_state.positions[0].maintenance_margin, 2.0, 1e-12);
}

TEST(OrderEntryServiceTest, PerpFullCloseSettlesRealizedPnlIntoWallet)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true, 1.0, 100.0));
    Account account(MakeLegacyCtorInitConfig(1000.0));

    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill close_fill{};
    close_fill.order_id = 2;
    close_fill.symbol = "BTCUSDT";
    close_fill.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    close_fill.side = QTrading::Dto::Trading::OrderSide::Sell;
    close_fill.position_side = QTrading::Dto::Trading::PositionSide::Both;
    close_fill.is_taker = false;
    close_fill.quantity = 1.0;
    close_fill.price = 110.0;

    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{ close_fill };
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        step_state,
        fills);

    EXPECT_TRUE(runtime_state.positions.empty());
    EXPECT_NEAR(account.get_wallet_balance(), 1009.978, 1e-9); // +10 realized -0.022 fee
}

TEST(OrderEntryServiceTest, PerpCloseRealizesLossIntoWallet)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true, 1.0, 100.0));
    Account account(MakeLegacyCtorInitConfig(1000.0));

    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill close_fill{};
    close_fill.order_id = 2;
    close_fill.symbol = "BTCUSDT";
    close_fill.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    close_fill.side = QTrading::Dto::Trading::OrderSide::Sell;
    close_fill.position_side = QTrading::Dto::Trading::PositionSide::Both;
    close_fill.is_taker = false;
    close_fill.quantity = 1.0;
    close_fill.price = 90.0;

    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{ close_fill };
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        step_state,
        fills);

    EXPECT_TRUE(runtime_state.positions.empty());
    EXPECT_NEAR(account.get_wallet_balance(), 989.982, 1e-9); // -10 realized -0.018 fee
}

TEST(OrderEntryServiceTest, PerpPartialCloseSettlesRealizedPnlIntoWallet)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true, 2.0, 100.0));
    Account account(MakeLegacyCtorInitConfig(1000.0));

    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill close_fill{};
    close_fill.order_id = 2;
    close_fill.symbol = "BTCUSDT";
    close_fill.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    close_fill.side = QTrading::Dto::Trading::OrderSide::Sell;
    close_fill.position_side = QTrading::Dto::Trading::PositionSide::Both;
    close_fill.is_taker = false;
    close_fill.quantity = 0.5;
    close_fill.price = 110.0;

    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{ close_fill };
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        step_state,
        fills);

    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_NEAR(runtime_state.positions[0].quantity, 1.5, 1e-12);
    EXPECT_NEAR(account.get_wallet_balance(), 1004.989, 1e-9); // +5 realized -0.011 fee
}

TEST(OrderEntryServiceTest, PerpReduceOnlyCloseSettlesRealizedPnlIntoWallet)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    runtime_state.positions.push_back(make_perp_position("BTCUSDT", false, 2.0, 100.0));
    Account account(MakeLegacyCtorInitConfig(1000.0));

    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill close_fill{};
    close_fill.order_id = 2;
    close_fill.symbol = "BTCUSDT";
    close_fill.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    close_fill.side = QTrading::Dto::Trading::OrderSide::Buy;
    close_fill.position_side = QTrading::Dto::Trading::PositionSide::Both;
    close_fill.reduce_only = true;
    close_fill.is_taker = false;
    close_fill.quantity = 1.0;
    close_fill.price = 90.0;

    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{ close_fill };
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        step_state,
        fills);

    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_FALSE(runtime_state.positions[0].is_long);
    EXPECT_NEAR(runtime_state.positions[0].quantity, 1.0, 1e-12);
    EXPECT_NEAR(account.get_wallet_balance(), 1009.982, 1e-9); // +10 realized -0.018 fee
}

TEST(OrderEntryServiceTest, PerpOneWayFlipSettlesClosingRealizedPnlBeforeOpeningOpposite)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true, 1.0, 100.0));
    Account account(MakeLegacyCtorInitConfig(1000.0));

    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill flip_fill{};
    flip_fill.order_id = 2;
    flip_fill.symbol = "BTCUSDT";
    flip_fill.instrument_type = QTrading::Dto::Trading::InstrumentType::Perp;
    flip_fill.side = QTrading::Dto::Trading::OrderSide::Sell;
    flip_fill.position_side = QTrading::Dto::Trading::PositionSide::Both;
    flip_fill.is_taker = false;
    flip_fill.quantity = 2.0;
    flip_fill.price = 90.0;

    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{ flip_fill };
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        step_state,
        fills);

    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_FALSE(runtime_state.positions[0].is_long);
    EXPECT_NEAR(runtime_state.positions[0].quantity, 1.0, 1e-12);
    EXPECT_NEAR(runtime_state.positions[0].entry_price, 90.0, 1e-12);
    EXPECT_NEAR(account.get_wallet_balance(), 989.964, 1e-9); // -10 realized -0.036 fee
}

TEST(OrderEntryServiceTest, SpotSellWithoutInventoryLeavesCashUnchanged)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_symbols({ "BTCUSDT", "ETHUSDT", "OPUSDT" });
    Account::AccountInitConfig init{};
    init.spot_initial_cash = 1000.0;
    init.perp_initial_wallet = 0.0;
    Account account(init);

    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill sell_fill{};
    sell_fill.order_id = 7;
    sell_fill.symbol = "OPUSDT";
    sell_fill.symbol_id = step_state.symbol_to_id.at("OPUSDT");
    sell_fill.instrument_type = QTrading::Dto::Trading::InstrumentType::Spot;
    sell_fill.side = QTrading::Dto::Trading::OrderSide::Sell;
    sell_fill.is_taker = false;
    sell_fill.quantity = 2.0;
    sell_fill.price = 10.0;

    const double cash_before = account.get_spot_balance().WalletBalance;
    EXPECT_THROW(
        QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
            runtime_state,
            account,
            step_state,
            std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill>{ sell_fill }),
        std::logic_error);
    EXPECT_NEAR(account.get_spot_balance().WalletBalance, cash_before, 1e-12);
    EXPECT_TRUE(runtime_state.positions.empty());
}

TEST(OrderEntryServiceTest, SpotSettlementUsesAuthoritativeStepSymbolIdAcrossMultiSymbolBook)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_symbols({ "BTCUSDT", "ETHUSDT", "OPUSDT" });
    runtime_state.positions.push_back(make_spot_position("OPUSDT", 5.0, 10.0));
    runtime_state.positions.push_back(make_spot_position("ETHUSDT", 3.0, 20.0));
    runtime_state.positions[1].id = 2;

    Account::AccountInitConfig init{};
    init.spot_initial_cash = 0.0;
    init.perp_initial_wallet = 0.0;
    Account account(init);

    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill sell_fill{};
    sell_fill.order_id = 8;
    sell_fill.symbol = "OPUSDT";
    sell_fill.symbol_id = step_state.symbol_to_id.at("OPUSDT");
    sell_fill.instrument_type = QTrading::Dto::Trading::InstrumentType::Spot;
    sell_fill.side = QTrading::Dto::Trading::OrderSide::Sell;
    sell_fill.is_taker = false;
    sell_fill.quantity = 5.0;
    sell_fill.price = 11.0;

    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        step_state,
        std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill>{ sell_fill });

    EXPECT_TRUE(runtime_state.positions.empty());
    ASSERT_EQ(runtime_state.spot_inventory_qty_by_symbol.size(), step_state.symbols.size());
    EXPECT_NEAR(
        runtime_state.spot_inventory_qty_by_symbol[step_state.symbol_to_id.at("OPUSDT")],
        0.0,
        1e-12);
    EXPECT_NEAR(
        runtime_state.spot_inventory_qty_by_symbol[step_state.symbol_to_id.at("ETHUSDT")],
        3.0,
        1e-12);
    EXPECT_NEAR(
        runtime_state.spot_inventory_entry_price_by_symbol[step_state.symbol_to_id.at("ETHUSDT")],
        20.0,
        1e-12);
    EXPECT_EQ(
        runtime_state.spot_inventory_position_id_by_symbol[step_state.symbol_to_id.at("OPUSDT")],
        0);
    EXPECT_EQ(
        runtime_state.spot_inventory_position_id_by_symbol[step_state.symbol_to_id.at("ETHUSDT")],
        2);
    EXPECT_NEAR(account.get_spot_balance().WalletBalance, 54.945, 1e-12);
}

TEST(OrderEntryServiceTest, TickPriceTimePriority_BuyHigherLimitFillsFirst)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_limit_request(1.0, 1100.0), reject));
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_limit_request(1.0, 1200.0), reject));
    ASSERT_EQ(runtime_state.orders.size(), 2u);

    const auto market = make_single_symbol_market("BTCUSDT", 1000.0, 1000.0, 1000.0, 1000.0, 1.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_NEAR(runtime_state.positions[0].entry_price, 1200.0, 1e-12);
    ASSERT_EQ(runtime_state.orders.size(), 1u);
    EXPECT_NEAR(runtime_state.orders[0].price, 1100.0, 1e-12);
}

TEST(OrderEntryServiceTest, TickPriceTimePriority_SamePriceLowerIdFillsFirst)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_limit_request(1.0, 1200.0), reject));
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_limit_request(1.0, 1200.0), reject));
    ASSERT_EQ(runtime_state.orders.size(), 2u);
    const int first_id = runtime_state.orders[0].id;
    const int second_id = runtime_state.orders[1].id;
    ASSERT_LT(first_id, second_id);

    const auto market = make_single_symbol_market("BTCUSDT", 1000.0, 1000.0, 1000.0, 1000.0, 1.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    ASSERT_EQ(runtime_state.orders.size(), 1u);
    EXPECT_EQ(runtime_state.orders[0].id, second_id);
}

TEST(OrderEntryServiceTest, OhlcTrigger_BuyLimitTriggersOnLow)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(50000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 95.0),
        reject));

    const auto market = make_single_symbol_market("BTCUSDT", 110.0, 120.0, 90.0, 105.0, 10.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_NEAR(runtime_state.positions[0].entry_price, 95.0, 1e-12);
}

TEST(OrderEntryServiceTest, OhlcTrigger_SellLimitTriggersOnHigh)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(50000.0));
    std::optional<OrderRejectInfo> reject{};

    auto request = make_perp_limit_request(1.0, 115.0);
    request.side = QTrading::Dto::Trading::OrderSide::Sell;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, request, reject));

    const auto market = make_single_symbol_market("BTCUSDT", 110.0, 120.0, 100.0, 105.0, 10.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_NEAR(runtime_state.positions[0].entry_price, 115.0, 1e-12);
}

TEST(OrderEntryServiceTest, IntraBarExpectedPath_SplitsOppositePassiveLimitVolume)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.hedge_mode = true;
    runtime_state.simulation_config.kline_volume_split_mode =
        QTrading::Infra::Exchanges::BinanceSim::Config::KlineVolumeSplitMode::OppositePassiveSplit;
    runtime_state.simulation_config.intra_bar_path_mode =
        QTrading::Infra::Exchanges::BinanceSim::Config::IntraBarPathMode::OpenMarketability;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    auto long_req = make_perp_limit_request(10.0, 95.0);
    long_req.position_side = QTrading::Dto::Trading::PositionSide::Long;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, long_req, reject));
    auto short_req = make_perp_limit_request(10.0, 105.0);
    short_req.side = QTrading::Dto::Trading::OrderSide::Sell;
    short_req.position_side = QTrading::Dto::Trading::PositionSide::Short;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, short_req, reject));

    auto market = make_single_symbol_market("BTCUSDT", 100.0, 110.0, 90.0, 100.0, 10.0, 0);
    ASSERT_TRUE(market.trade_klines_by_id[0].has_value());
    market.trade_klines_by_id[0]->TakerBuyBaseVolume = 5.0;
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    double long_qty = 0.0;
    double short_qty = 0.0;
    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp) {
            continue;
        }
        if (position.is_long) {
            long_qty += position.quantity;
        }
        else {
            short_qty += position.quantity;
        }
    }
    EXPECT_NEAR(long_qty, 5.0, 1e-8);
    EXPECT_NEAR(short_qty, 5.0, 1e-8);
}

TEST(OrderEntryServiceTest, MarketOrderKeepsArrivalPriorityAgainstPricedOrders)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    auto market_request = make_perp_market_request(1.0);
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, market_request, reject));
    ASSERT_EQ(runtime_state.orders.size(), 1u);
    const int market_id = runtime_state.orders[0].id;

    auto limit_request = make_perp_limit_request(1.0, 1200.0);
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, limit_request, reject));
    ASSERT_EQ(runtime_state.orders.size(), 2u);
    const int limit_id = runtime_state.orders[1].id;

    const auto market = make_single_symbol_market("BTCUSDT", 1000.0, 1000.0, 1000.0, 1000.0, 1.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].order_id, market_id);
    ASSERT_EQ(runtime_state.orders.size(), 1u);
    EXPECT_EQ(runtime_state.orders[0].id, limit_id);
}

TEST(OrderEntryServiceTest, IntraBarPathModeUsesOpenMarketabilityForTakerClassification)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.simulation_config.intra_bar_path_mode =
        QTrading::Infra::Exchanges::BinanceSim::Config::IntraBarPathMode::OpenMarketability;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 100.0),
        reject));

    const auto market = make_single_symbol_market("BTCUSDT", 105.0, 106.0, 95.0, 99.0, 1000.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_FALSE(fills[0].is_taker);
}

TEST(OrderEntryServiceTest, IntraBarMonteCarloPathWithFixedSeedIsDeterministic)
{
    auto run_once = [](uint64_t seed) -> double {
        BinanceExchangeRuntimeState runtime_state{};
        runtime_state.hedge_mode = true;
        runtime_state.simulation_config.kline_volume_split_mode =
            QTrading::Infra::Exchanges::BinanceSim::Config::KlineVolumeSplitMode::OppositePassiveSplit;
        runtime_state.simulation_config.intra_bar_path_mode =
            QTrading::Infra::Exchanges::BinanceSim::Config::IntraBarPathMode::MonteCarloPath;
        runtime_state.simulation_config.intra_bar_random_seed = seed;
        runtime_state.simulation_config.intra_bar_monte_carlo_samples = 257u;
        StepKernelState step_state = make_step_state_with_perp_symbol();
        Account account(MakeLegacyCtorInitConfig(100000.0));
        std::optional<OrderRejectInfo> reject{};

        auto long_req = make_perp_limit_request(10.0, 95.0);
        long_req.position_side = QTrading::Dto::Trading::PositionSide::Long;
        EXPECT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, long_req, reject));
        auto short_req = make_perp_limit_request(10.0, 105.0);
        short_req.side = QTrading::Dto::Trading::OrderSide::Sell;
        short_req.position_side = QTrading::Dto::Trading::PositionSide::Short;
        EXPECT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, short_req, reject));

        auto market = make_single_symbol_market("BTCUSDT", 100.0, 110.0, 90.0, 100.0, 10.0, 0);
        if (!market.trade_klines_by_id[0].has_value()) {
            return -1.0;
        }
        market.trade_klines_by_id[0]->TakerBuyBaseVolume = -1.0;
        std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
        QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
            runtime_state,
            step_state,
            market,
            fills);
        QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
            runtime_state,
            account,
            fills);

        double long_qty = 0.0;
        for (const auto& position : runtime_state.positions) {
            if (position.instrument_type == QTrading::Dto::Trading::InstrumentType::Perp &&
                position.is_long) {
                long_qty += position.quantity;
            }
        }
        return long_qty;
    };

    const double first = run_once(42ull);
    const double second = run_once(42ull);
    EXPECT_NEAR(first, second, 1e-12);
    EXPECT_GT(first, 0.0);
    EXPECT_LT(first, 10.0);
}

TEST(OrderEntryServiceTest, IntraBarMonteCarloPathUsesOpenMarketabilityForTakerClassification)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.simulation_config.intra_bar_path_mode =
        QTrading::Infra::Exchanges::BinanceSim::Config::IntraBarPathMode::MonteCarloPath;
    runtime_state.simulation_config.intra_bar_random_seed = 42ull;
    runtime_state.simulation_config.intra_bar_monte_carlo_samples = 17u;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 100.0),
        reject));

    const auto market = make_single_symbol_market("BTCUSDT", 105.0, 106.0, 95.0, 99.0, 1000.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_FALSE(fills[0].is_taker);
}

TEST(OrderEntryServiceTest, TickVolumeSplit_UsesTakerBuyBaseVolumePools)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.hedge_mode = true;
    runtime_state.simulation_config.kline_volume_split_mode =
        QTrading::Infra::Exchanges::BinanceSim::Config::KlineVolumeSplitMode::OppositePassiveSplit;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    auto long_req = make_perp_limit_request(10.0, 95.0);
    long_req.position_side = QTrading::Dto::Trading::PositionSide::Long;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, long_req, reject));
    auto short_req = make_perp_limit_request(10.0, 105.0);
    short_req.side = QTrading::Dto::Trading::OrderSide::Sell;
    short_req.position_side = QTrading::Dto::Trading::PositionSide::Short;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, short_req, reject));

    auto market = make_single_symbol_market("BTCUSDT", 100.0, 110.0, 90.0, 100.0, 10.0, 0);
    ASSERT_TRUE(market.trade_klines_by_id[0].has_value());
    market.trade_klines_by_id[0]->TakerBuyBaseVolume = 7.0;
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    double long_qty = 0.0;
    double short_qty = 0.0;
    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp) {
            continue;
        }
        if (position.is_long) {
            long_qty += position.quantity;
        }
        else {
            short_qty += position.quantity;
        }
    }
    EXPECT_NEAR(long_qty, 3.0, 1e-8);
    EXPECT_NEAR(short_qty, 7.0, 1e-8);
}

TEST(OrderEntryServiceTest, TickVolumeSplit_Heuristic_CloseNearHighBiasesSellOrders)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.hedge_mode = true;
    runtime_state.simulation_config.kline_volume_split_mode =
        QTrading::Infra::Exchanges::BinanceSim::Config::KlineVolumeSplitMode::OppositePassiveSplit;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    auto long_req = make_perp_limit_request(10.0, 95.0);
    long_req.position_side = QTrading::Dto::Trading::PositionSide::Long;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, long_req, reject));
    auto short_req = make_perp_limit_request(10.0, 105.0);
    short_req.side = QTrading::Dto::Trading::OrderSide::Sell;
    short_req.position_side = QTrading::Dto::Trading::PositionSide::Short;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, short_req, reject));

    auto market = make_single_symbol_market("BTCUSDT", 100.0, 110.0, 90.0, 109.0, 10.0, 0);
    ASSERT_TRUE(market.trade_klines_by_id[0].has_value());
    market.trade_klines_by_id[0]->TakerBuyBaseVolume = -1.0;
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    double long_qty = 0.0;
    double short_qty = 0.0;
    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp) {
            continue;
        }
        if (position.is_long) {
            long_qty += position.quantity;
        }
        else {
            short_qty += position.quantity;
        }
    }
    EXPECT_NEAR(long_qty, 0.5, 1e-6);
    EXPECT_NEAR(short_qty, 9.5, 1e-6);
}

TEST(OrderEntryServiceTest, TickVolumeSplit_Heuristic_CloseNearLowBiasesBuyOrders)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.hedge_mode = true;
    runtime_state.simulation_config.kline_volume_split_mode =
        QTrading::Infra::Exchanges::BinanceSim::Config::KlineVolumeSplitMode::OppositePassiveSplit;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    auto long_req = make_perp_limit_request(10.0, 95.0);
    long_req.position_side = QTrading::Dto::Trading::PositionSide::Long;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, long_req, reject));
    auto short_req = make_perp_limit_request(10.0, 105.0);
    short_req.side = QTrading::Dto::Trading::OrderSide::Sell;
    short_req.position_side = QTrading::Dto::Trading::PositionSide::Short;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, short_req, reject));

    auto market = make_single_symbol_market("BTCUSDT", 100.0, 110.0, 90.0, 91.0, 10.0, 0);
    ASSERT_TRUE(market.trade_klines_by_id[0].has_value());
    market.trade_klines_by_id[0]->TakerBuyBaseVolume = -1.0;
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    double long_qty = 0.0;
    double short_qty = 0.0;
    for (const auto& position : runtime_state.positions) {
        if (position.instrument_type != QTrading::Dto::Trading::InstrumentType::Perp) {
            continue;
        }
        if (position.is_long) {
            long_qty += position.quantity;
        }
        else {
            short_qty += position.quantity;
        }
    }
    EXPECT_NEAR(long_qty, 9.5, 1e-6);
    EXPECT_NEAR(short_qty, 0.5, 1e-6);
}

TEST(OrderEntryServiceTest, OpenOrderInitialMargin_MarketOrderUsesVisibleReferencePrice)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.symbol_leverage["BTCUSDT"] = 10.0;
    runtime_state.last_status_snapshot.prices.push_back(
        QTrading::Infra::Exchanges::BinanceSim::Contracts::StatusPriceSnapshot{
            "BTCUSDT", 0.0, false, 0.0, false, 100.0, true });
    StepKernelState step_state = make_step_state_with_perp_symbol();
    step_state.symbol_spec_by_id[0].trigger_protect = 0.0;
    step_state.symbol_spec_by_id[0].market_take_bound = 0.0;
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_market_request(2.0),
        reject));
    EXPECT_FALSE(reject.has_value());
    EXPECT_NEAR(runtime_state.perp_open_order_initial_margin, (2.0 * 100.0) / 10.0, 1e-9);
}

TEST(OrderEntryServiceTest, PerpLimitOrderRejectsWhenInitialMarginExceedsAvailableBalance)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.symbol_leverage["BTCUSDT"] = 10.0;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10.0));
    std::optional<OrderRejectInfo> reject{};

    auto request = make_perp_limit_request(2.0, 100.0); // required margin 20 > available 10
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, request, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::PerpInsufficientMargin);
    EXPECT_TRUE(runtime_state.orders.empty());
}

TEST(OrderEntryServiceTest, PerpMarketOrderRejectsWhenInitialMarginExceedsAvailableBalance)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.symbol_leverage["BTCUSDT"] = 10.0;
    runtime_state.last_status_snapshot.prices.push_back(
        QTrading::Infra::Exchanges::BinanceSim::Contracts::StatusPriceSnapshot{
            "BTCUSDT", 0.0, false, 0.0, false, 100.0, true });
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(19.0));
    std::optional<OrderRejectInfo> reject{};

    auto request = make_perp_market_request(2.0); // required margin 20 > available 19
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, request, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::PerpInsufficientMargin);
    EXPECT_TRUE(runtime_state.orders.empty());
}

TEST(OrderEntryServiceTest, PerpMarketOrderAcceptsAtExactVisibleAvailableBalanceBoundary)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.symbol_leverage["BTCUSDT"] = 10.0;
    runtime_state.last_status_snapshot.prices.push_back(
        QTrading::Infra::Exchanges::BinanceSim::Contracts::StatusPriceSnapshot{
            "BTCUSDT", 0.0, false, 0.0, false, 100.0, true });
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(20.0));
    std::optional<OrderRejectInfo> reject{};

    EXPECT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_market_request(2.0), reject));
    EXPECT_FALSE(reject.has_value());
    ASSERT_EQ(runtime_state.orders.size(), 1u);
    EXPECT_NEAR(runtime_state.perp_open_order_initial_margin, 20.0, 1e-12);
}

TEST(OrderEntryServiceTest, PerpAdmissionUsesVisibleAvailableBalanceWhenFixtureStateDiverges)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.symbol_leverage["BTCUSDT"] = 10.0;
    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true, 1.0, 100.0));
    runtime_state.positions.back().initial_margin = 80.0;
    runtime_state.last_status_snapshot.prices.push_back(
        QTrading::Infra::Exchanges::BinanceSim::Contracts::StatusPriceSnapshot{
            "BTCUSDT", 0.0, false, 0.0, false, 100.0, true });

    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100.0));
    account.update_perp_mark_state(/*unrealized_pnl=*/0.0, /*position_initial_margin=*/0.0, /*maintenance_margin=*/0.0);
    account.sync_open_order_initial_margins(/*spot=*/0.0, /*perp=*/0.0);
    ASSERT_NEAR(account.get_perp_balance().AvailableBalance, 100.0, 1e-12);

    std::optional<OrderRejectInfo> reject{};
    auto request = make_perp_limit_request(1.0, 100.0); // incremental margin 10 should fit visible available balance 100
    EXPECT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, request, reject));
    EXPECT_FALSE(reject.has_value());
    ASSERT_EQ(runtime_state.orders.size(), 1u);
}

TEST(OrderEntryServiceTest, OpenOrderInitialMargin_OneWayClosingDirectionDoesNotReserveMargin)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.symbol_leverage["BTCUSDT"] = 10.0;
    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true, 2.0, 100.0));
    StepKernelState step_state = make_step_state_with_perp_symbol();
    step_state.symbol_spec_by_id[0].trigger_protect = 0.0;
    step_state.symbol_spec_by_id[0].market_take_bound = 0.0;
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    auto request = make_perp_limit_request(1.0, 100.0);
    request.side = QTrading::Dto::Trading::OrderSide::Sell;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, request, reject));
    EXPECT_FALSE(reject.has_value());
    EXPECT_NEAR(runtime_state.perp_open_order_initial_margin, 0.0, 1e-12);
}

TEST(OrderEntryServiceTest, OpenOrderInitialMargin_OneWayFlipReservesOnlyForOpeningOvershoot)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.symbol_leverage["BTCUSDT"] = 10.0;
    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true, 2.0, 100.0));
    StepKernelState step_state = make_step_state_with_perp_symbol();
    step_state.symbol_spec_by_id[0].trigger_protect = 0.0;
    step_state.symbol_spec_by_id[0].market_take_bound = 0.0;
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    auto request = make_perp_limit_request(5.0, 100.0);
    request.side = QTrading::Dto::Trading::OrderSide::Sell;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, request, reject));
    EXPECT_FALSE(reject.has_value());
    EXPECT_NEAR(runtime_state.perp_open_order_initial_margin, 30.0, 1e-12);
}

TEST(OrderEntryServiceTest, OpenOrderInitialMargin_OneWayAggregatesMultipleOppositeOrders)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.symbol_leverage["BTCUSDT"] = 10.0;
    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true, 2.0, 100.0));
    StepKernelState step_state = make_step_state_with_perp_symbol();
    step_state.symbol_spec_by_id[0].trigger_protect = 0.0;
    step_state.symbol_spec_by_id[0].market_take_bound = 0.0;
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    auto first = make_perp_limit_request(2.0, 100.0);
    first.side = QTrading::Dto::Trading::OrderSide::Sell;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, first, reject));
    EXPECT_NEAR(runtime_state.perp_open_order_initial_margin, 0.0, 1e-12);

    auto second = make_perp_limit_request(2.0, 100.0);
    second.side = QTrading::Dto::Trading::OrderSide::Sell;
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, second, reject));
    EXPECT_NEAR(runtime_state.perp_open_order_initial_margin, 20.0, 1e-12);
}

TEST(OrderEntryServiceTest, LimitFillProbabilityModelUsesPenetrationAndSizeRatio)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.simulation_config.limit_fill_probability_enabled = true;
    runtime_state.simulation_config.limit_fill_probability_bias = 1.0;
    runtime_state.simulation_config.limit_fill_probability_penetration_weight = 2.0;
    runtime_state.simulation_config.limit_fill_probability_size_weight = 2.0;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_limit_request(10.0, 99.0), reject));
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_limit_request(10.0, 91.0), reject));
    auto market = make_single_symbol_market("BTCUSDT", 100.0, 110.0, 90.0, 100.0, 100.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(runtime_state, step_state, market, fills);
    ASSERT_EQ(fills.size(), 2u);

    const QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill* deep = nullptr;
    const QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill* shallow = nullptr;
    for (const auto& fill : fills) {
        if (std::abs(fill.price - 99.0) < 1e-12) {
            deep = &fill;
        }
        if (std::abs(fill.price - 91.0) < 1e-12) {
            shallow = &fill;
        }
    }
    ASSERT_NE(deep, nullptr);
    ASSERT_NE(shallow, nullptr);
    EXPECT_GT(deep->fill_probability, shallow->fill_probability);
    EXPECT_GT(deep->quantity, shallow->quantity);
    const double deep_prob = deep->fill_probability;

    BinanceExchangeRuntimeState runtime_state_large{};
    runtime_state_large.simulation_config.limit_fill_probability_enabled = true;
    runtime_state_large.simulation_config.limit_fill_probability_bias = 1.0;
    runtime_state_large.simulation_config.limit_fill_probability_penetration_weight = 2.0;
    runtime_state_large.simulation_config.limit_fill_probability_size_weight = 2.0;
    StepKernelState step_state_large = make_step_state_with_perp_symbol();
    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state_large,
        account,
        step_state_large,
        make_perp_limit_request(80.0, 99.0),
        reject));
    fills.clear();
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state_large,
        step_state_large,
        market,
        fills);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_LT(fills[0].fill_probability, deep_prob);
}

TEST(OrderEntryServiceTest, MarketOrderFill_UsesExecutionSlippageBoundedByOHLC)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.simulation_config.market_execution_slippage = 0.10;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_market_request(1.0), reject));
    auto market = make_single_symbol_market("BTCUSDT", 100.0, 105.0, 95.0, 100.0, 1000.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(runtime_state, step_state, market, fills);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_NEAR(fills[0].price, 105.0, 1e-12);
}

TEST(OrderEntryServiceTest, LimitOrderFill_UsesExecutionSlippageButRespectsLimit)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.simulation_config.limit_execution_slippage = 0.10;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(50000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_limit_request(1.0, 100.0), reject));
    auto market = make_single_symbol_market("BTCUSDT", 95.0, 110.0, 90.0, 95.0, 1000.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(runtime_state, step_state, market, fills);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_NEAR(fills[0].price, 100.0, 1e-12);
}

TEST(OrderEntryServiceTest, LimitOrderFill_ExecutionSlippageCanWorsenPriceWithinLimit)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.simulation_config.limit_execution_slippage = 0.10;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(50000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_limit_request(1.0, 110.0), reject));
    auto market = make_single_symbol_market("BTCUSDT", 95.0, 110.0, 90.0, 95.0, 1000.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(runtime_state, step_state, market, fills);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_NEAR(fills[0].price, 104.5, 1e-12);
}

TEST(OrderEntryServiceTest, MarketImpactSlippageCurveWorsensLargeOrderMoreThanSmallOrder)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.simulation_config.market_impact_slippage_enabled = true;
    runtime_state.simulation_config.market_impact_base_bps = 0.0;
    runtime_state.simulation_config.market_impact_max_bps = 500.0;
    runtime_state.simulation_config.market_impact_size_exponent = 1.0;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_market_request(1.0), reject));
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_market_request(8.0), reject));
    auto market = make_single_symbol_market("BTCUSDT", 100.0, 120.0, 80.0, 100.0, 100.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(runtime_state, step_state, market, fills);
    ASSERT_EQ(fills.size(), 2u);

    const QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill* small = nullptr;
    const QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill* large = nullptr;
    for (const auto& fill : fills) {
        if (std::abs(fill.order_quantity - 1.0) < 1e-12) {
            small = &fill;
        }
        if (std::abs(fill.order_quantity - 8.0) < 1e-12) {
            large = &fill;
        }
    }
    ASSERT_NE(small, nullptr);
    ASSERT_NE(large, nullptr);
    EXPECT_GT(large->impact_slippage_bps, small->impact_slippage_bps);
    EXPECT_GT(large->price, small->price);
}

TEST(OrderEntryServiceTest, MarketImpactSlippageRespectsLimitProtection)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.simulation_config.market_impact_slippage_enabled = true;
    runtime_state.simulation_config.market_impact_base_bps = 0.0;
    runtime_state.simulation_config.market_impact_max_bps = 5000.0;
    runtime_state.simulation_config.market_impact_size_exponent = 1.0;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_limit_request(8.0, 100.0), reject));
    auto market = make_single_symbol_market("BTCUSDT", 105.0, 110.0, 95.0, 100.0, 100.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(runtime_state, step_state, market, fills);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_LE(fills[0].price, 100.0 + 1e-12);
}

TEST(OrderEntryServiceTest, TakerProbabilityModelUsesDiscreteFeeRate)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.simulation_config.taker_probability_model_enabled = true;
    runtime_state.simulation_config.taker_probability_bias = -1.0;
    runtime_state.simulation_config.taker_probability_penetration_weight = 2.0;
    runtime_state.simulation_config.taker_probability_size_weight = 0.5;
    runtime_state.simulation_config.taker_probability_taker_weight = 0.5;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(100000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, make_perp_limit_request(1.0, 99.0), reject));
    auto market = make_single_symbol_market("BTCUSDT", 100.0, 110.0, 90.0, 100.0, 100.0, 0);
    ASSERT_TRUE(market.trade_klines_by_id[0].has_value());
    market.trade_klines_by_id[0]->TakerBuyBaseVolume = 90.0;
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(runtime_state, step_state, market, fills);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_GT(fills[0].taker_probability, 0.0);
    EXPECT_LT(fills[0].taker_probability, 1.0);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(runtime_state, account, fills);
    ASSERT_EQ(runtime_state.positions.size(), 1u);
    const double expected_rate = fills[0].is_taker ? 0.0005 : 0.0002;
    EXPECT_NEAR(runtime_state.positions[0].fee_rate, expected_rate, 1e-12);
}

TEST(OrderEntryServiceTest, CancelOrderByIdRemovesRemainingOpenOrderAndKeepsFilledPosition)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(5000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(5.0, 500.0),
        reject));
    EXPECT_FALSE(reject.has_value());
    ASSERT_EQ(runtime_state.orders.size(), 1u);
    const int order_id = runtime_state.orders[0].id;

    const auto market = make_single_symbol_market("BTCUSDT", 500.0, 500.0, 500.0, 500.0, 2.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);

    ASSERT_EQ(runtime_state.orders.size(), 1u);
    EXPECT_DOUBLE_EQ(runtime_state.orders[0].quantity, 3.0);
    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_DOUBLE_EQ(runtime_state.positions[0].quantity, 2.0);

    EXPECT_TRUE(OrderEntryService::CancelOrderById(runtime_state, step_state, order_id));
    EXPECT_TRUE(runtime_state.orders.empty());
    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_DOUBLE_EQ(runtime_state.positions[0].quantity, 2.0);
}

TEST(OrderEntryServiceTest, ClientOrderIdMustBeUniqueAmongOpenOrders)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    auto first = make_perp_limit_request(1.0, 100.0);
    first.client_order_id = "cid-1";
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, first, reject));
    EXPECT_FALSE(reject.has_value());
    ASSERT_EQ(runtime_state.orders.size(), 1u);
    EXPECT_EQ(runtime_state.orders[0].client_order_id, "cid-1");

    auto second = make_perp_limit_request(1.0, 99.0);
    second.side = QTrading::Dto::Trading::OrderSide::Sell;
    second.client_order_id = "cid-1";
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, second, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::DuplicateClientOrderId);
    EXPECT_EQ(runtime_state.orders.size(), 1u);
}

TEST(OrderEntryServiceTest, StpExpireTakerRejectsCrossingIncomingOrder)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 100.0),
        reject));
    EXPECT_FALSE(reject.has_value());

    auto incoming = make_perp_limit_request(1.0, 99.0);
    incoming.side = QTrading::Dto::Trading::OrderSide::Sell;
    incoming.stp_mode = static_cast<int>(Account::SelfTradePreventionMode::ExpireTaker);
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, incoming, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::StpExpiredTaker);
    ASSERT_EQ(runtime_state.orders.size(), 1u);
    EXPECT_EQ(runtime_state.orders[0].side, QTrading::Dto::Trading::OrderSide::Buy);
}

TEST(OrderEntryServiceTest, StpExpireMakerCancelsConflictingRestingOrder)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 100.0),
        reject));
    EXPECT_FALSE(reject.has_value());

    auto incoming = make_perp_limit_request(1.0, 99.0);
    incoming.side = QTrading::Dto::Trading::OrderSide::Sell;
    incoming.stp_mode = static_cast<int>(Account::SelfTradePreventionMode::ExpireMaker);
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, incoming, reject));
    EXPECT_FALSE(reject.has_value());
    ASSERT_EQ(runtime_state.orders.size(), 1u);
    EXPECT_EQ(runtime_state.orders[0].side, QTrading::Dto::Trading::OrderSide::Sell);
}

TEST(OrderEntryServiceTest, StpExpireBothCancelsRestingAndRejectsIncomingOrder)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 100.0),
        reject));
    EXPECT_FALSE(reject.has_value());

    auto incoming = make_perp_limit_request(1.0, 99.0);
    incoming.side = QTrading::Dto::Trading::OrderSide::Sell;
    incoming.stp_mode = static_cast<int>(Account::SelfTradePreventionMode::ExpireBoth);
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, incoming, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::StpExpiredBoth);
    EXPECT_TRUE(runtime_state.orders.empty());
}

TEST(OrderEntryServiceTest, StpUsesSymbolDefaultModeWhenRequestModeIsNone)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.default_stp_mode = static_cast<int>(Account::SelfTradePreventionMode::ExpireMaker);
    spec.allowed_stp_modes_mask = static_cast<uint8_t>(
        (1u << static_cast<uint8_t>(Account::SelfTradePreventionMode::None)) |
        (1u << static_cast<uint8_t>(Account::SelfTradePreventionMode::ExpireMaker)));
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    ASSERT_TRUE(OrderEntryService::Execute(
        runtime_state,
        account,
        step_state,
        make_perp_limit_request(1.0, 100.0),
        reject));
    EXPECT_FALSE(reject.has_value());

    auto incoming = make_perp_limit_request(1.0, 99.0);
    incoming.side = QTrading::Dto::Trading::OrderSide::Sell;
    incoming.stp_mode = static_cast<int>(Account::SelfTradePreventionMode::None);
    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, incoming, reject));
    EXPECT_FALSE(reject.has_value());
    ASSERT_EQ(runtime_state.orders.size(), 1u);
    EXPECT_EQ(runtime_state.orders[0].side, QTrading::Dto::Trading::OrderSide::Sell);
    EXPECT_EQ(
        runtime_state.orders[0].stp_mode,
        static_cast<int>(Account::SelfTradePreventionMode::ExpireMaker));
}

TEST(OrderEntryServiceTest, StpRejectsModeNotAllowedBySymbolPolicy)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.default_stp_mode = static_cast<int>(Account::SelfTradePreventionMode::ExpireMaker);
    spec.allowed_stp_modes_mask = static_cast<uint8_t>(
        (1u << static_cast<uint8_t>(Account::SelfTradePreventionMode::None)) |
        (1u << static_cast<uint8_t>(Account::SelfTradePreventionMode::ExpireMaker)));
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    auto request = make_perp_limit_request(1.0, 100.0);
    request.stp_mode = static_cast<int>(Account::SelfTradePreventionMode::ExpireTaker);
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, request, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::StpModeNotAllowed);
    EXPECT_TRUE(runtime_state.orders.empty());
}

TEST(OrderEntryServiceTest, StpRejectsInvalidModeValue)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    auto request = make_perp_limit_request(1.0, 100.0);
    request.stp_mode = 99;
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, request, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::InvalidStpMode);
    EXPECT_TRUE(runtime_state.orders.empty());
}

TEST(OrderEntryServiceTest, SwitchingModeWithoutPositionsSucceeds)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    std::optional<OrderRejectInfo> reject{};

    EXPECT_TRUE(OrderEntryService::SetPositionMode(runtime_state, step_state, true, reject));
    EXPECT_FALSE(reject.has_value());
    EXPECT_TRUE(runtime_state.hedge_mode);
}

TEST(OrderEntryServiceTest, SwitchingModeWithOpenPositionsFails)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true));

    std::optional<OrderRejectInfo> reject{};
    EXPECT_FALSE(OrderEntryService::SetPositionMode(runtime_state, step_state, true, reject));
    EXPECT_TRUE(reject.has_value());
    EXPECT_FALSE(runtime_state.hedge_mode);
}

TEST(OrderEntryServiceTest, SwitchingModeWithOpenOrdersFails)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    runtime_state.orders.push_back(QTrading::dto::Order{
        1,
        "BTCUSDT",
        1.0,
        100.0,
        QTrading::Dto::Trading::OrderSide::Buy,
        QTrading::Dto::Trading::PositionSide::Both,
        false,
        -1,
        QTrading::Dto::Trading::InstrumentType::Perp,
        {},
        0,
        false,
        0.0,
        false });

    std::optional<OrderRejectInfo> reject{};
    EXPECT_FALSE(OrderEntryService::SetPositionMode(runtime_state, step_state, true, reject));
    EXPECT_TRUE(reject.has_value());
    EXPECT_FALSE(runtime_state.hedge_mode);
}

TEST(OrderEntryServiceTest, HedgeMode_OrderRequiresExplicitPositionSide_IsRejectedWithoutException)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.hedge_mode = true;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    auto request = make_perp_limit_request(1.0, 100.0);
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, request, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::HedgeModePositionSideRequired);
    EXPECT_TRUE(runtime_state.orders.empty());
}

TEST(OrderEntryServiceTest, HedgeModeReduceOnlyOrderRejectedByDefault)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.hedge_mode = true;
    runtime_state.strict_binance_mode = true;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true));

    auto request = make_perp_limit_request(0.5, 100.0);
    request.side = QTrading::Dto::Trading::OrderSide::Sell;
    request.position_side = QTrading::Dto::Trading::PositionSide::Long;
    request.reduce_only = true;

    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, request, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::StrictHedgeReduceOnlyDisabled);
    EXPECT_TRUE(runtime_state.orders.empty());
}

TEST(OrderEntryServiceTest, CompatibilityModeAllowsHedgeReduceOnlyWhenStrictDisabled)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.hedge_mode = true;
    runtime_state.strict_binance_mode = false;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true));

    auto request = make_perp_limit_request(0.5, 100.0);
    request.side = QTrading::Dto::Trading::OrderSide::Sell;
    request.position_side = QTrading::Dto::Trading::PositionSide::Long;
    request.reduce_only = true;

    ASSERT_TRUE(OrderEntryService::Execute(runtime_state, account, step_state, request, reject));
    EXPECT_FALSE(reject.has_value());
    ASSERT_EQ(runtime_state.orders.size(), 1u);
    EXPECT_DOUBLE_EQ(runtime_state.orders[0].quantity, 0.5);

    const auto market = make_single_symbol_market("BTCUSDT", 100.0, 100.0, 100.0, 100.0, 1.0, 0);
    std::vector<QTrading::Infra::Exchanges::BinanceSim::Domain::MatchFill> fills{};
    QTrading::Infra::Exchanges::BinanceSim::Domain::MatchingEngine::RunStep(
        runtime_state,
        step_state,
        market,
        fills);
    QTrading::Infra::Exchanges::BinanceSim::Domain::FillSettlementEngine::Apply(
        runtime_state,
        account,
        fills);
    ASSERT_EQ(runtime_state.positions.size(), 1u);
    EXPECT_NEAR(runtime_state.positions[0].quantity, 0.5, 1e-12);
}

TEST(OrderEntryServiceTest, HedgeModeReduceOnly_WrongSideOrNoMatchingPosition_IsRejectedWithoutException)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.hedge_mode = true;
    runtime_state.strict_binance_mode = false;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true));

    auto wrong_side = make_perp_limit_request(0.5, 100.0);
    wrong_side.side = QTrading::Dto::Trading::OrderSide::Buy;
    wrong_side.position_side = QTrading::Dto::Trading::PositionSide::Long;
    wrong_side.reduce_only = true;
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, wrong_side, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::ReduceOnlyNoReduciblePosition);

    auto no_match = make_perp_limit_request(0.5, 100.0);
    no_match.side = QTrading::Dto::Trading::OrderSide::Buy;
    no_match.position_side = QTrading::Dto::Trading::PositionSide::Short;
    no_match.reduce_only = true;
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, no_match, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::ReduceOnlyNoReduciblePosition);
}

TEST(OrderEntryServiceTest, ReduceOnly_HedgeMode_RequiresExplicitPositionSide)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.hedge_mode = true;
    runtime_state.strict_binance_mode = false;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true));

    auto request = make_perp_limit_request(0.5, 100.0);
    request.side = QTrading::Dto::Trading::OrderSide::Sell;
    request.reduce_only = true;
    request.position_side = QTrading::Dto::Trading::PositionSide::Both;

    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, request, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::HedgeModePositionSideRequired);
}

TEST(OrderEntryServiceTest, ReduceOnly_HedgeMode_DirectionMustCloseCorrectSide)
{
    BinanceExchangeRuntimeState runtime_state{};
    runtime_state.hedge_mode = true;
    runtime_state.strict_binance_mode = false;
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    runtime_state.positions.push_back(make_perp_position("BTCUSDT", true));
    runtime_state.positions.push_back(make_perp_position("BTCUSDT", false));

    auto wrong_long = make_perp_limit_request(0.5, 100.0);
    wrong_long.side = QTrading::Dto::Trading::OrderSide::Buy;
    wrong_long.position_side = QTrading::Dto::Trading::PositionSide::Long;
    wrong_long.reduce_only = true;
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, wrong_long, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::ReduceOnlyNoReduciblePosition);

    auto wrong_short = make_perp_limit_request(0.5, 100.0);
    wrong_short.side = QTrading::Dto::Trading::OrderSide::Sell;
    wrong_short.position_side = QTrading::Dto::Trading::PositionSide::Short;
    wrong_short.reduce_only = true;
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, wrong_short, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::ReduceOnlyNoReduciblePosition);
}

TEST(OrderEntryServiceTest, ClosePositionRejectsInvalidBinanceParameterCombinations)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(MakeLegacyCtorInitConfig(10000.0));
    std::optional<OrderRejectInfo> reject{};

    auto missing_close_flag = make_perp_close_position_request();
    missing_close_flag.close_position = false;
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, missing_close_flag, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::ClosePositionInvalidParameters);

    auto with_reduce_only = make_perp_close_position_request();
    with_reduce_only.reduce_only = true;
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, with_reduce_only, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::ClosePositionInvalidParameters);

    auto with_quantity = make_perp_close_position_request();
    with_quantity.quantity = 1.0;
    EXPECT_FALSE(OrderEntryService::Execute(runtime_state, account, step_state, with_quantity, reject));
    ASSERT_TRUE(reject.has_value());
    EXPECT_EQ(reject->code, OrderRejectInfo::Code::ClosePositionInvalidParameters);
}
