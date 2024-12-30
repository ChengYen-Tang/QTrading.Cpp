// account.cpp
#include "exanges/binance_simulator/futures/account.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <limits>

Account::Account(double initial_balance, int vip_level)
    : balance(initial_balance), used_margin(0.0), leverage(100.0), max_leverage_allowed(100.0), vip_level(vip_level) {
    // 初始化手续费率
    get_fee_rates();
}

void Account::set_leverage(double lev) {
    if (lev > 0 && lev <= max_leverage_allowed) {
        leverage = lev;
        std::cout << "杠杆已设置为 " << leverage << "x" << std::endl;
    }
    else {
        std::cerr << "杠杆倍数超出允许范围（最大杠杆：" << max_leverage_allowed << "x）。" << std::endl;
    }
}

double Account::get_balance() const {
    return balance;
}

void Account::place_order(const std::string& symbol, double quantity, double price, bool is_long, bool is_market_order) {
    // 获取当前手续费率
    double maker_fee_rate, taker_fee_rate;
    std::tie(maker_fee_rate, taker_fee_rate) = get_fee_rates();

    // 选择手续费率
    double fee_rate = is_market_order ? taker_fee_rate : maker_fee_rate;

    // 计算名义价值
    double notional = price * quantity;

    // 获取动态的保证金率和最大杠杆
    double initial_margin_rate, maintenance_margin_rate, max_leverage;
    std::tie(initial_margin_rate, maintenance_margin_rate, max_leverage) = get_margin_rates(notional);

    // 更新最大杠杆限制
    max_leverage_allowed = max_leverage;

    // 检查用户设置的杠杆是否超出最大杠杆
    if (leverage > max_leverage_allowed) {
        std::cerr << "当前仓位允许的最大杠杆为 " << max_leverage_allowed << "x，请调整杠杆。" << std::endl;
        return;
    }

    // 根据实际杠杆计算初始保证金率
    initial_margin_rate = 1.0 / leverage;

    // 计算初始保证金
    double initial_margin = notional * initial_margin_rate;

    // 计算维持保证金
    double maintenance_margin = notional * maintenance_margin_rate;

    // 计算手续费
    double fee = notional * fee_rate;

    // 检查余额是否足够支付初始保证金和手续费
    if (balance < initial_margin + fee) {
        std::cerr << "余额不足，无法下单。" << std::endl;
        return;
    }

    // 扣除初始保证金和手续费
    balance -= (initial_margin + fee);
    used_margin += initial_margin;

    // 创建头寸
    Position pos = {
        symbol,
        quantity,
        price,
        is_long,
        0.0,
        notional,
        initial_margin,
        maintenance_margin,
        fee,
        leverage,
        fee_rate
    };
    positions.push_back(pos);

    std::cout << "下单成功：" << (is_long ? "做多" : "做空") << " " << quantity << " 手 " << symbol
        << " @ $" << price << (is_market_order ? "（市价单）" : "（限价单）") << std::endl;
    std::cout << "初始保证金率: " << initial_margin_rate * 100 << "%, 初始保证金: $" << initial_margin
        << ", 维持保证金: $" << maintenance_margin << ", 手续费: $" << fee << std::endl;
}

void Account::update_positions(const MarketData::Kline& kline) {
    // 更新每个头寸的盈亏
    for (auto& pos : positions) {
        double market_price = kline.ClosePrice;
        double pnl = (market_price - pos.entry_price) * pos.quantity * (pos.is_long ? 1 : -1);
        pos.unrealized_pnl = pnl;

        // 计算保证金余额
        double wallet_balance = balance + total_unrealized_pnl();
        double margin_balance = wallet_balance; // 这里假设没有未实现的资金费用

        // 计算维持保证金
        double maintenance_margin = pos.maintenance_margin;

        if (margin_balance <= maintenance_margin) {
            // 触发爆仓
            std::cout << "头寸爆仓：" << pos.symbol << std::endl;
            // 扣除剩余保证金作为清算费用
            balance = 0;
            positions.clear();
            used_margin = 0;
            return;
        }
    }
}

void Account::close_position(const std::string& symbol, double price, bool is_market_order) {
    // 获取当前手续费率
    double maker_fee_rate, taker_fee_rate;
    std::tie(maker_fee_rate, taker_fee_rate) = get_fee_rates();

    // 选择手续费率
    double fee_rate = is_market_order ? taker_fee_rate : maker_fee_rate;

    // 寻找对应的头寸并平仓
    for (auto it = positions.begin(); it != positions.end(); ++it) {
        if (it->symbol == symbol) {
            double notional = price * it->quantity;
            double fee = notional * fee_rate;

            // 更新余额
            balance += (it->initial_margin + it->unrealized_pnl - fee);
            used_margin -= it->initial_margin;

            std::cout << "头寸已平仓：" << symbol << (is_market_order ? "（市价单）" : "（限价单）") << std::endl;
            std::cout << "平仓收益: $" << it->unrealized_pnl << ", 手续费: $" << fee << std::endl;

            positions.erase(it);
            return;
        }
    }
    std::cerr << "未找到符号为 " << symbol << " 的头寸。" << std::endl;
}

double Account::total_unrealized_pnl() const {
    double total_pnl = 0.0;
    for (const auto& pos : positions) {
        total_pnl += pos.unrealized_pnl;
    }
    return total_pnl;
}

std::tuple<double, double, double> Account::get_margin_rates(double notional) const {
    for (const auto& tier : margin_tiers) {
        if (notional <= tier.notional_upper) {
            return std::make_tuple(tier.initial_margin_rate, tier.maintenance_margin_rate, tier.max_leverage);
        }
    }
    // 默认值
    return std::make_tuple(0.125, 0.075, 8);
}

std::tuple<double, double> Account::get_fee_rates() const {
    auto it = vip_fee_rates.find(vip_level);
    if (it != vip_fee_rates.end()) {
        return std::make_tuple(it->second.maker_fee_rate, it->second.taker_fee_rate);
    }
    else {
        // 如果 VIP 等级未找到，默认使用 VIP 0 的费率
        return std::make_tuple(0.0002, 0.0004);
    }
}
