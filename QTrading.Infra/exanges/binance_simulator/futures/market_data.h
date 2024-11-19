#pragma once

#include <string>
#include <vector>

class MarketData {
public:
    struct Kline {
        std::string timestamp;
        double open;
        double high;
        double low;
        double close;
        double volume;
    };

    MarketData(const std::string& symbol, const std::string& csv_file);

    const Kline& get_latest_kline() const;
    const Kline& get_kline(size_t index) const;
    size_t get_klines_count() const;

private:
    std::string symbol;
    std::vector<Kline> klines;

    void load_csv(const std::string& csv_file);
};
