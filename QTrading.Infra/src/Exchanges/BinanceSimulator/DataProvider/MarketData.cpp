#include "Exchanges/BinanceSimulator/DataProvider/MarketData.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& utf8)
{
    if (utf8.empty()) {
        return std::wstring();
    }

    auto convert = [](UINT codepage, const std::string& input) -> std::wstring {
        const int len = MultiByteToWideChar(codepage, 0, input.data(),
            static_cast<int>(input.size()), nullptr, 0);
        if (len <= 0) {
            return std::wstring();
        }
        std::wstring out(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(codepage, 0, input.data(),
            static_cast<int>(input.size()), out.data(), len);
        return out;
    };

    std::wstring out = convert(CP_UTF8, utf8);
    if (!out.empty()) {
        return out;
    }

    return convert(CP_ACP, utf8);
}
#endif

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

static bool parse_kline_line(std::string_view line, TradeKlineDto& out)
{
    // Supported formats:
    // - 11 columns: openTime,open,high,low,close,volume,closeTime,quoteVolume,trades,takerBuyBaseVol,takerBuyQuoteVol
    // - 6 columns:  openTime,open,high,low,close,closeTime
    std::string_view s = line;
    if (s.empty()) {
        return false;
    }

    std::string_view fields[11]{};
    size_t count = 0;
    while (!s.empty() && count < 11) {
        fields[count++] = next_field(s);
    }
    if (count < 6) {
        return false;
    }

    long long openTs = 0;
    double openP = 0.0;
    double highP = 0.0;
    double lowP = 0.0;
    double closeP = 0.0;
    double vol = 0.0;
    long long closeTs = 0;
    double quoteVol = 0.0;
    int trades = 0;
    double takerBB = 0.0;
    double takerBQ = 0.0;

    if (!parse_int<long long>(fields[0], openTs)) return false;
    if (!parse_double(fields[1], openP)) return false;
    if (!parse_double(fields[2], highP)) return false;
    if (!parse_double(fields[3], lowP)) return false;
    if (!parse_double(fields[4], closeP)) return false;

    if (count >= 11) {
        if (!parse_double(fields[5], vol)) return false;
        if (!parse_int<long long>(fields[6], closeTs)) return false;
        if (!parse_double(fields[7], quoteVol)) return false;
        if (!parse_int<int>(fields[8], trades)) return false;
        if (!parse_double(fields[9], takerBB)) return false;
        if (!parse_double(fields[10], takerBQ)) return false;
    }
    else {
        if (!parse_int<long long>(fields[5], closeTs)) return false;
    }

    out = TradeKlineDto(openTs, openP, highP, lowP, closeP, vol, closeTs, quoteVol, trades, takerBB, takerBQ);
    return true;
}

} // namespace

/// @brief Construct MarketData for a given symbol by loading CSV.
/// @param symbol Trading symbol (e.g., "BTCUSDT").
/// @param csv_file Path to 1-minute Kline CSV.
MarketData::MarketData(const std::string& symbol, const std::string& csv_file) : symbol(symbol) {
    load_csv(csv_file);
}

/// @brief Load TradeKlineDto entries from a CSV file into `klines`.
///        Lines with insufficient tokens or parse errors are skipped.
/// @param csv_file Path to CSV file.
void MarketData::load_csv(const std::string& csv_file) {
#ifdef _WIN32
    const std::filesystem::path path(utf8_to_wide(csv_file));
#else
    const std::filesystem::path path(csv_file);
#endif
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + csv_file);
    }

    klines.clear();
    std::error_code ec;
    const auto file_bytes = std::filesystem::file_size(path, ec);
    if (!ec && file_bytes > 0) {
        constexpr uintmax_t kAvgBytesPerRow = 80;
        uintmax_t est_rows = file_bytes / kAvgBytesPerRow;
        if (est_rows < 1024) {
            est_rows = 1024;
        }
        const uintmax_t size_t_max = static_cast<uintmax_t>((std::numeric_limits<size_t>::max)());
        if (est_rows > size_t_max) {
            est_rows = size_t_max;
        }
        klines.reserve(static_cast<size_t>(est_rows));
    }
    else {
        klines.reserve(1 << 16);
    }

    std::string line;

    // Skip header
    if (!std::getline(file, line)) {
        throw std::runtime_error("CSV file is empty or cannot read header: " + csv_file);
    }

    bool monotonic = true;
    uint64_t last_ts = 0;
    bool has_last = false;

    while (std::getline(file, line)) {
        TradeKlineDto k{};
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
            [](const TradeKlineDto& a, const TradeKlineDto& b) {
                return a.Timestamp < b.Timestamp;
            });
    }
}

/// @brief Get the most recent Kline entry.
/// @return Reference to the last element in `klines`.
const TradeKlineDto& MarketData::get_latest_kline() const {
    if (klines.empty()) {
        throw std::out_of_range("No kline data available");
    }
    return klines.back();
}

/// @brief Get the Kline at a specific index.
/// @param index Zero-based position in `klines`.
/// @return Reference to the TradeKlineDto at `index`.
/// @throws std::out_of_range if index is invalid.
const TradeKlineDto& MarketData::get_kline(size_t index) const {
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

size_t MarketData::lower_bound_ts(uint64_t ts) const
{
    const auto it = std::lower_bound(klines.begin(), klines.end(), ts,
        [](const TradeKlineDto& entry, uint64_t value) {
            return entry.Timestamp < value;
        });
    return static_cast<size_t>(std::distance(klines.begin(), it));
}

size_t MarketData::upper_bound_ts(uint64_t ts) const
{
    const auto it = std::upper_bound(klines.begin(), klines.end(), ts,
        [](uint64_t value, const TradeKlineDto& entry) {
            return value < entry.Timestamp;
        });
    return static_cast<size_t>(std::distance(klines.begin(), it));
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
