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

    using iterator = std::vector<KlineDto>::iterator;
    using const_iterator = std::vector<KlineDto>::const_iterator;

    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    const_iterator cbegin() const;
    const_iterator cend() const;

private:
    std::string symbol;
    std::vector<KlineDto> klines;

    void load_csv(const std::string& csv_file);
};
