#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>

#include "Data/Binance/MarketData.hpp"
#include "Dto/Trading/InstrumentSpec.hpp"
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
