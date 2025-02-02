#include "exanges/BinanceSimulator/DataProvider/MarketData.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/stream.hpp>
#include <stdexcept>

MarketData::MarketData(const std::string& symbol, const std::string& csv_file) : symbol(symbol) {
    load_csv(csv_file);
}

void MarketData::load_csv(const std::string& csv_file) {
    namespace io = boost::iostreams;

    // 使用 Boost.IOStreams 打開文件
    io::file_source file_source(csv_file);
    if (!file_source.is_open()) {
        throw std::runtime_error("Cannot open file: " + csv_file);
    }

    io::stream<io::file_source> file(file_source);
    std::string line;

    if (!std::getline(file, line)) {
        throw std::runtime_error("CSV file is empty or cannot read header: " + csv_file);
    }

    while (std::getline(file, line)) {
        std::vector<std::string> tokens;
        boost::split(tokens, line, boost::is_any_of(","));

        if (tokens.size() < 11) continue;

        try {
            KlineDto kline = {
                std::stoll(tokens[0]),
                std::stod(tokens[1]),
                std::stod(tokens[2]),
                std::stod(tokens[3]),
                std::stod(tokens[4]),
                std::stod(tokens[5]),
                std::stoll(tokens[6]),
                std::stod(tokens[7]),
                std::stoi(tokens[8]),
                std::stod(tokens[9]),
                std::stod(tokens[10])
            };
            klines.push_back(kline);
        }
        catch (const std::invalid_argument& e) {
            continue;
        }
        catch (const std::out_of_range& e) {
            continue;
        }
    }
}

const KlineDto& MarketData::get_latest_kline() const {
    return klines.back();
}

const KlineDto& MarketData::get_kline(size_t index) const {
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
