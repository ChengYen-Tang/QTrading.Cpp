#pragma once

#include <string>
#include <vector>
#include "Dto/Market/Binance/Kline.hpp"

using namespace QTrading::Dto::Market::Binance;

/// @brief Provides historical Kline data loaded from a CSV file.
/// 
/// This class loads 1-minute KlineDto entries for a given symbol from a CSV,
/// stores them in chronological order, and offers random access and iteration.
class MarketData {
public:
    /// @brief Construct MarketData for a symbol from a CSV file.
    /// @param symbol Trading symbol identifier (e.g. "BTCUSDT").
    /// @param csv_file Filesystem path to the CSV containing Kline rows.
    MarketData(const std::string& symbol, const std::string& csv_file);

    /// @brief Get the latest (most recent) Kline entry.
    /// @return Const reference to the last KlineDto in the loaded data.
    const KlineDto& get_latest_kline() const;

    /// @brief Get the Kline at a specific index.
    /// @param index Zero-based index into the time-ordered Kline vector.
    /// @return Const reference to the KlineDto at the given index.
    /// @throw std::out_of_range if index ≥ total count.
    const KlineDto& get_kline(size_t index) const;

    /// @brief Get the total number of Kline entries loaded.
    /// @return Size of the internal Kline vector.
    size_t get_klines_count() const;

    /// @brief Iterator over mutable KlineDto entries.
    using iterator = std::vector<KlineDto>::iterator;
    /// @brief Iterator over const KlineDto entries.
    using const_iterator = std::vector<KlineDto>::const_iterator;

    /// @brief Get iterator to first Kline.
    iterator begin();
    /// @brief Get iterator one past last Kline.
    iterator end();
    /// @brief Get const iterator to first Kline.
    const_iterator begin() const;
    /// @brief Get const iterator one past last Kline.
    const_iterator end() const;
    /// @brief Get const iterator to first Kline.
    const_iterator cbegin() const;
    /// @brief Get const iterator one past last Kline.
    const_iterator cend() const;

private:
    std::string symbol;         ///< Trading symbol
    std::vector<KlineDto> klines;///< In-memory bar storage

    /// @brief Load and parse the CSV file into `klines`.
    /// @param csv_file Path to the CSV file.
    void load_csv(const std::string& csv_file);
};
