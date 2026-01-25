#pragma once

#include <string>
#include <vector>
#include "Dto/Market/Binance/FundingRate.hpp"

using namespace QTrading::Dto::Market::Binance;

/// @brief Provides historical funding-rate data loaded from a CSV file.
/// @details Loads funding events for a given symbol in chronological order.
class FundingRateData {
public:
    /// @brief Construct FundingRateData for a symbol from a CSV file.
    /// @param symbol Trading symbol identifier (e.g. "BTCUSDT").
    /// @param csv_file Filesystem path to the CSV containing funding rows.
    FundingRateData(const std::string& symbol, const std::string& csv_file);

    /// @brief Get the latest (most recent) funding entry.
    /// @return Const reference to the last FundingRateDto in the loaded data.
    const FundingRateDto& get_latest() const;

    /// @brief Get the funding entry at a specific index.
    /// @param index Zero-based index into the time-ordered funding vector.
    /// @return Const reference to the FundingRateDto at the given index.
    /// @throw std::out_of_range if index >= total count.
    const FundingRateDto& get_funding(size_t index) const;

    /// @brief Get the total number of funding entries loaded.
    /// @return Size of the internal funding vector.
    size_t get_count() const;

    /// @brief Upper bound index for a funding time (first index with FundingTime > ts).
    size_t upper_bound_ts(uint64_t ts) const;

    /// @brief Iterator over mutable FundingRateDto entries.
    using iterator = std::vector<FundingRateDto>::iterator;
    /// @brief Iterator over const FundingRateDto entries.
    using const_iterator = std::vector<FundingRateDto>::const_iterator;

    /// @brief Get iterator to first funding entry.
    iterator begin();
    /// @brief Get iterator one past last funding entry.
    iterator end();
    /// @brief Get const iterator to first funding entry.
    const_iterator begin() const;
    /// @brief Get const iterator one past last funding entry.
    const_iterator end() const;
    /// @brief Get const iterator to first funding entry.
    const_iterator cbegin() const;
    /// @brief Get const iterator one past last funding entry.
    const_iterator cend() const;

private:
    std::string symbol;                 ///< Trading symbol
    std::vector<FundingRateDto> rates;  ///< In-memory funding storage

    /// @brief Load and parse the CSV file into `rates`.
    /// @param csv_file Path to the CSV file.
    void load_csv(const std::string& csv_file);
};
