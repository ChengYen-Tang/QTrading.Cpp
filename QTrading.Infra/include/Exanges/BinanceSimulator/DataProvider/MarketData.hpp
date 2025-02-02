#pragma once

#include <string>
#include <vector>
#include <Dto/Market/Binance/Kline.hpp>

using namespace QTrading::Dto::Market::Binance;

class MarketData {
public:
    MarketData(const std::string& symbol, const std::string& csv_file);

    const KlineDto& get_latest_kline() const;
    const KlineDto& get_kline(size_t index) const;
    size_t get_klines_count() const;

private:
    std::string symbol;
    std::vector<KlineDto> klines;

    void load_csv(const std::string& csv_file);
};
