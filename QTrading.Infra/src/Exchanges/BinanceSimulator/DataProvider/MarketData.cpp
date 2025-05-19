#include "Exchanges/BinanceSimulator/DataProvider/MarketData.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/stream.hpp>
#include <stdexcept>

/// @brief Construct MarketData for a given symbol by loading CSV.
/// @param symbol Trading symbol (e.g., "BTCUSDT").
/// @param csv_file Path to 1-minute Kline CSV.
MarketData::MarketData(const std::string& symbol, const std::string& csv_file) : symbol(symbol) {
    load_csv(csv_file);
}

/// @brief Load KlineDto entries from a CSV file into `klines`.
///        Lines with insufficient tokens or parse errors are skipped.
/// @param csv_file Path to CSV file.
void MarketData::load_csv(const std::string& csv_file) {
    namespace io = boost::iostreams;

    io::file_source file_source(csv_file);
    if (!file_source.is_open()) {
        throw std::runtime_error("Cannot open file: " + csv_file);
    }

    io::stream<io::file_source> file(file_source);
    std::string line;

    // Skip header
    if (!std::getline(file, line)) {
        throw std::runtime_error("CSV file is empty or cannot read header: " + csv_file);
    }

    while (std::getline(file, line)) {
        std::vector<std::string> tokens;
        boost::split(tokens, line, boost::is_any_of(","));

        if (tokens.size() < 11) continue;

        try {
            auto openTs = std::stoll(tokens[0]);
            auto openP = std::stod(tokens[1]);
            auto highP = std::stod(tokens[2]);
            auto lowP = std::stod(tokens[3]);
            auto closeP = std::stod(tokens[4]);
            auto vol = std::stod(tokens[5]);
            auto closeTs = std::stoll(tokens[6]);
            auto quoteVol = std::stod(tokens[7]);
            auto trades = std::stoi(tokens[8]);
            auto takerBB = std::stod(tokens[9]);
            auto takerBQ = std::stod(tokens[10]);

            klines.emplace_back(
                openTs, openP, highP, lowP, closeP, vol,
                closeTs, quoteVol, trades, takerBB, takerBQ);
        }
        catch (const std::invalid_argument& e) {
            continue;
        }
        catch (const std::out_of_range& e) {
            continue;
        }
    }

    // Ensure chronological order
	std::sort(klines.begin(), klines.end(),
		[](const KlineDto& a, const KlineDto& b) {
			return a.Timestamp < b.Timestamp;
		});
}

/// @brief Get the most recent Kline entry.
/// @return Reference to the last element in `klines`.
const KlineDto& MarketData::get_latest_kline() const {
    return klines.back();
}

/// @brief Get the Kline at a specific index.
/// @param index Zero-based position in `klines`.
/// @return Reference to the KlineDto at `index`.
/// @throws std::out_of_range if index is invalid.
const KlineDto& MarketData::get_kline(size_t index) const {
    if (index < klines.size()) {
        return klines[index];
    }
    else {
        throw std::out_of_range("Kline index out of range");
    }
}

/// @brief Get the number of loaded Kline entries.
/// @return Size of the `klines` vector.
size_t MarketData::get_klines_count() const {
    return klines.size();
}

/// @brief Iterator to first mutable Kline.
/// @return `klines.begin()`
MarketData::iterator MarketData::begin() {
	return klines.begin();
}

/// @brief Iterator one past last mutable Kline.
/// @return `klines.end()`
MarketData::iterator MarketData::end() {
	return klines.end();
}

/// @brief Iterator to first const Kline.
/// @return `klines.begin()`
MarketData::const_iterator MarketData::begin() const {
	return klines.begin();
}

/// @brief Iterator one past last const Kline.
/// @return `klines.end()`
MarketData::const_iterator MarketData::end() const {
	return klines.end();
}

/// @brief Const iterator to first Kline.
/// @return `klines.cbegin()`
MarketData::const_iterator MarketData::cbegin() const {
	return klines.cbegin();
}

/// @brief Const iterator one past last Kline.
/// @return `klines.cend()`
MarketData::const_iterator MarketData::cend() const {
	return klines.cend();
}
