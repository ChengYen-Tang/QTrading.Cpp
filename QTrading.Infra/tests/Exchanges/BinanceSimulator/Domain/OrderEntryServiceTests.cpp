#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
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

StepKernelState make_step_state_with_perp_symbol()
{
    StepKernelState state{};
    state.symbols.push_back("BTCUSDT");
    state.symbol_to_id.emplace("BTCUSDT", 0);
    state.symbol_instrument_type_by_id.push_back(QTrading::Dto::Trading::InstrumentType::Perp);
    state.symbol_spec_by_id.push_back(QTrading::Dto::Trading::PerpInstrumentSpec());
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

} // namespace

TEST(OrderEntryServiceTest, InstrumentFiltersRejectInvalidPriceTickAndRange)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.min_price = 100.0;
    spec.max_price = 200.0;
    spec.price_tick_size = 0.1;
    Account account(10000.0, 0);
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
    Account account(10000.0, 0);
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
    Account account(10000.0, 0);
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

TEST(OrderEntryServiceTest, InstrumentFiltersPerpMarketNotionalUsesMarkPrice)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    auto& spec = step_state.symbol_spec_by_id[0];
    spec.min_notional = 150.0;
    Account account(10000.0, 0);
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

TEST(OrderEntryServiceTest, ImmediatelyExecutableLimitIsTakerFee)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(10000.0, 0);
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
    Account account(10000.0, 0);
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

TEST(OrderEntryServiceTest, TickPriceTimePriority_BuyHigherLimitFillsFirst)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(100000.0, 0);
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
    Account account(100000.0, 0);
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
    Account account(50000.0, 0);
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
    Account account(50000.0, 0);
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
    Account account(100000.0, 0);
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
    Account account(100000.0, 0);
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
    Account account(100000.0, 0);
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

TEST(OrderEntryServiceTest, CancelOrderByIdRemovesRemainingOpenOrderAndKeepsFilledPosition)
{
    BinanceExchangeRuntimeState runtime_state{};
    StepKernelState step_state = make_step_state_with_perp_symbol();
    Account account(5000.0, 0);
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
    Account account(10000.0, 0);
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
    Account account(10000.0, 0);
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
    Account account(10000.0, 0);
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
    Account account(10000.0, 0);
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
    Account account(10000.0, 0);
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
    Account account(10000.0, 0);
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
    Account account(10000.0, 0);
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
    Account account(10000.0, 0);
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
    Account account(10000.0, 0);
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
    Account account(10000.0, 0);
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
