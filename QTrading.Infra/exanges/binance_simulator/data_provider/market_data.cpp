#include "market_data.h"
#include <fstream>
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <stdexcept>

MarketData::MarketData(const std::string& symbol, const std::string& csv_file) : symbol(symbol) {
    load_csv(csv_file);
}

void MarketData::load_csv(const std::string& csv_file) {
    std::ifstream file(csv_file);
    if (!file.is_open()) {
        throw std::runtime_error("无法打开文件：" + csv_file);
    }

    std::string line;
    // 跳过 CSV 文件的标题行
    std::getline(file, line);
    while (std::getline(file, line)) {
        std::vector<std::string> tokens;
        boost::split(tokens, line, boost::is_any_of(","));
        if (tokens.size() < 6) continue;
        Kline kline = {
            tokens[0],
            std::stod(tokens[1]),
            std::stod(tokens[2]),
            std::stod(tokens[3]),
            std::stod(tokens[4]),
            std::stod(tokens[5]),
			tokens[6],
			std::stod(tokens[7]),
			std::stoi(tokens[8]),
			std::stod(tokens[9]),
			std::stod(tokens[10])
        };
        klines.push_back(kline);
    }
}

const MarketData::Kline& MarketData::get_latest_kline() const {
    return klines.back();
}

const MarketData::Kline& MarketData::get_kline(size_t index) const {
    if (index < klines.size()) {
        return klines[index];
    }
    else {
        throw std::out_of_range("Kline index out of range");
    }
}

size_t MarketData::get_klines_count() const {
    return klines.size();
}
