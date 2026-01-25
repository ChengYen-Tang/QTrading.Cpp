#include "Execution/MarketExecutionEngine.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <unordered_map>

using MarketPtr = std::shared_ptr<QTrading::Dto::Market::Binance::MultiKlineDto>;

namespace {

class FakeExchange final : public QTrading::Infra::Exchanges::IExchange<MarketPtr> {
public:
    bool place_order(const std::string&, double, double,
        QTrading::Dto::Trading::OrderSide,
        QTrading::Dto::Trading::PositionSide,
        bool) override { return true; }
    bool place_order(const std::string&, double,
        QTrading::Dto::Trading::OrderSide,
        QTrading::Dto::Trading::PositionSide,
        bool) override { return true; }
    void close_position(const std::string&, double) override {}
    void close_position(const std::string&) override {}
    void close_position(const std::string&,
        QTrading::Dto::Trading::PositionSide,
        double) override {}
    bool step() override { return false; }
    const std::vector<QTrading::dto::Position>& get_all_positions() const override { return positions_; }
    const std::vector<QTrading::dto::Order>& get_all_open_orders() const override { return orders_; }
    void set_symbol_leverage(const std::string& symbol, double new_leverage) override
    {
        leverage_[symbol] = new_leverage;
    }
    double get_symbol_leverage(const std::string& symbol) const override
    {
        auto it = leverage_.find(symbol);
        return (it == leverage_.end()) ? 1.0 : it->second;
    }

    std::vector<QTrading::dto::Position> positions_;
    std::vector<QTrading::dto::Order> orders_;
    std::unordered_map<std::string, double> leverage_;
};

MarketPtr MakeMarket(unsigned long long ts, const std::string& symbol, double close)
{
    auto dto = std::make_shared<QTrading::Dto::Market::Binance::MultiKlineDto>();
    dto->Timestamp = ts;
    QTrading::Dto::Market::Binance::KlineDto k(ts, 0, 0, 0, close, 0, ts, 0, 0, 0, 0);
    auto symbols = std::make_shared<std::vector<std::string>>();
    symbols->push_back(symbol);
    dto->symbols = symbols;
    dto->klines_by_id.resize(symbols->size());
    dto->klines_by_id[0] = k;
    return dto;
}

} // namespace

TEST(MarketExecutionEngineTests, GeneratesOrderForNotionalDelta)
{
    auto ex = std::make_shared<FakeExchange>();
    QTrading::dto::Position pos{};
    pos.symbol = "BTCUSDT_PERP";
    pos.quantity = 1.0;
    pos.is_long = true;
    ex->positions_.push_back(pos);

    QTrading::Execution::MarketExecutionEngine engine(ex, { 10.0 });
    QTrading::Risk::RiskTarget target;
    target.target_positions["BTCUSDT_PERP"] = 1000.0;
    target.leverage["BTCUSDT_PERP"] = 3.0;

    QTrading::Signal::SignalDecision signal;
    auto orders = engine.plan(target, signal, MakeMarket(1, "BTCUSDT_PERP", 10000.0));

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].action, QTrading::Execution::OrderAction::Sell);
    EXPECT_NEAR(orders[0].qty, 0.9, 1e-6);
    EXPECT_TRUE(orders[0].reduce_only);
    EXPECT_DOUBLE_EQ(ex->get_symbol_leverage("BTCUSDT_PERP"), 3.0);
}
