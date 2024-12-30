#pragma once

#include <map>

/// <summary>
/// Handling fee definition, based on VIP level
/// </summary>
struct FeeRate {
    double maker_fee_rate;
    double taker_fee_rate;
};

/// <summary>
/// Define the mapping between VIP levels and handling rates
/// </summary>
const std::map<int, FeeRate> vip_fee_rates = {
    {0, {0.0002, 0.0004}},   // VIP 0
    {1, {0.00018, 0.00036}}, // VIP 1
    {2, {0.00016, 0.00034}}, // VIP 2
    {3, {0.00014, 0.00032}}, // VIP 3
    {4, {0.00012, 0.00030}}, // VIP 4
    {5, {0.00010, 0.00028}}, // VIP 5
    {6, {0.00008, 0.00026}}, // VIP 6
    {7, {0.00006, 0.00024}}, // VIP 7
    {8, {0.00004, 0.00022}}, // VIP 8
    {9, {0.00002, 0.00020}}  // VIP 9
};

// 保证金分层结构
struct MarginTier {
    double notional_upper;          // 名义价值上限
    double initial_margin_rate;     // 初始保证金率
    double maintenance_margin_rate; // 维持保证金率
    double max_leverage;            // 最大杠杆
};

// 定义保证金分层表
const std::vector<MarginTier> margin_tiers = {
    {50000,    0.01,  0.005, 100},
    {250000,   0.02,  0.01,   50},
    {1000000,  0.03,  0.015,  33},
    {5000000,  0.05,  0.025,  20},
    {10000000, 0.10,  0.05,   10},
    {std::numeric_limits<double>::max(), 0.125, 0.075, 8}
};
