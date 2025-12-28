#include "Exchanges/BinanceSimulator/DataProvider/MarketData.hpp"
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/stream.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace {

static inline std::string_view next_field(std::string_view& s)
{
    const size_t pos = s.find(',');
    if (pos == std::string_view::npos) {
        auto out = s;
        s = std::string_view{};
        return out;
    }
    auto out = s.substr(0, pos);
    s.remove_prefix(pos + 1);
    return out;
}

template <typename T>
static bool parse_int(std::string_view sv, T& out)
{
    sv = std::string_view{ sv.data(), sv.size() };
    if constexpr (std::is_signed_v<T>) {
        T v{};
        auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
        if (ec != std::errc{} || p != sv.data() + sv.size()) return false;
        out = v;
        return true;
    }
    else {
        T v{};
        auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
        if (ec != std::errc{} || p != sv.data() + sv.size()) return false;
        out = v;
        return true;
    }
}

static bool parse_double(std::string_view sv, double& out)
{
    // MSVC supports from_chars for floating point.
    double v{};
    auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
    if (ec != std::errc{} || p != sv.data() + sv.size()) return false;
    out = v;
    return true;
}

static bool parse_kline_line(std::string_view line, KlineDto& out)
{
    // Expected columns (at least 11):
    // 0 openTime,1 open,2 high,3 low,4 close,5 volume,6 closeTime,7 quoteVolume,8 trades,9 takerBuyBaseVol,10 takerBuyQuoteVol
    std::string_view s = line;

    long long openTs = 0;
    double openP = 0.0, highP = 0.0, lowP = 0.0, closeP = 0.0, vol = 0.0;
    long long closeTs = 0;
    double quoteVol = 0.0;
    int trades = 0;
    double takerBB = 0.0, takerBQ = 0.0;

    if (s.empty()) return false;

    auto f0 = next_field(s);
    auto f1 = next_field(s);
    auto f2 = next_field(s);
    auto f3 = next_field(s);
    auto f4 = next_field(s);
    auto f5 = next_field(s);
    auto f6 = next_field(s);
    auto f7 = next_field(s);
    auto f8 = next_field(s);
    auto f9 = next_field(s);
    auto f10 = next_field(s);

    // If we couldn't get 11 fields, reject.
    if (f10.data() == nullptr || f0.empty() || f1.empty() || f2.empty() || f3.empty() || f4.empty()) {
        return false;
    }

    if (!parse_int<long long>(f0, openTs)) return false;
    if (!parse_double(f1, openP)) return false;
    if (!parse_double(f2, highP)) return false;
    if (!parse_double(f3, lowP)) return false;
    if (!parse_double(f4, closeP)) return false;
    if (!parse_double(f5, vol)) return false;
    if (!parse_int<long long>(f6, closeTs)) return false;
    if (!parse_double(f7, quoteVol)) return false;
    if (!parse_int<int>(f8, trades)) return false;
    if (!parse_double(f9, takerBB)) return false;
    if (!parse_double(f10, takerBQ)) return false;

    out = KlineDto(openTs, openP, highP, lowP, closeP, vol, closeTs, quoteVol, trades, takerBB, takerBQ);
    return true;
}

} // namespace

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

    // Simple reserve heuristic: for backtests, 1-minute data is large.
    // Reserve some upfront to reduce reallocations; will grow as needed.
    klines.clear();
    klines.reserve(1 << 16);

    std::string line;

    // Skip header
    if (!std::getline(file, line)) {
        throw std::runtime_error("CSV file is empty or cannot read header: " + csv_file);
    }

    bool monotonic = true;
    uint64_t last_ts = 0;
    bool has_last = false;

    while (std::getline(file, line)) {
        KlineDto k{};
        if (!parse_kline_line(std::string_view(line), k)) {
            continue;
        }

        if (has_last) {
            if (k.Timestamp < last_ts) {
                monotonic = false;
            }
        }
        last_ts = k.Timestamp;
        has_last = true;

        klines.emplace_back(std::move(k));
    }

    // Ensure chronological order only when needed.
    if (!monotonic) {
        std::sort(klines.begin(), klines.end(),
            [](const KlineDto& a, const KlineDto& b) {
                return a.Timestamp < b.Timestamp;
            });
    }
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
