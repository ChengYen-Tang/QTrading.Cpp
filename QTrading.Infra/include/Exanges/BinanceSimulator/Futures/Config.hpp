#pragma once

#include <map>
#include <limits>
#include <vector>

// Fee rate structure for maker/taker
struct FeeRate {
    double maker_fee_rate;
    double taker_fee_rate;
};

// Mapping from VIP level to fee rates
const std::map<int, FeeRate> vip_fee_rates = {
    {0, {0.00020, 0.00050}}, // VIP 0
    {1, {0.00016, 0.00040}}, // VIP 1
    {2, {0.00014, 0.00035}}, // VIP 2
    {3, {0.00012, 0.00032}}, // VIP 3
    {4, {0.00010, 0.00030}}, // VIP 4
    {5, {0.00008, 0.00027}}, // VIP 5
    {6, {0.00006, 0.00025}}, // VIP 6
    {7, {0.00004, 0.00022}}, // VIP 7
    {8, {0.00002, 0.00020}}, // VIP 8
    {9, {0.00000, 0.00017}}  // VIP 9
};

// Margin tier definition
struct MarginTier {
    double notional_upper;          // Upper limit of notional value
    double maintenance_margin_rate; // Maintenance margin rate
    double max_leverage;            // Maximum leverage
};

// Tier-based margin configuration
const std::vector<MarginTier> margin_tiers = {
    {50000,      0.0040, 125},
    {600000,     0.0050, 100},
    {3000000,    0.0065,  75},
    {12000000,   0.0100,  50},
    {70000000,   0.0200,  25},
    {100000000,  0.0250,  20},
    {230000000,  0.0500,  10},
    {480000000,  0.1000,   5},
    {600000000,  0.1250,   4},
    {800000000,  0.1500,   3},
    {1200000000, 0.2500,   2},
    {std::numeric_limits<double>::max(), 0.5000, 1}
};
