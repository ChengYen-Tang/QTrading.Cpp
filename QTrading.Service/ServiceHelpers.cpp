#include "ServiceHelpers.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>

namespace QTrading::Service::Helpers {

namespace {

std::atomic<bool> g_stop_requested{ false };

void HandleSignal(int)
{
    g_stop_requested.store(true, std::memory_order_relaxed);
}

} // namespace

std::string JsonEscape(const std::string& input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (const char c : input) {
        if (c == '\\') {
            out += "\\\\";
        }
        else if (c == '"') {
            out += "\\\"";
        }
        else {
            out.push_back(c);
        }
    }
    return out;
}

std::string Utf8Path(const char8_t* path)
{
    return std::string(reinterpret_cast<const char*>(path));
}

void SetEnvVar(const char* key, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}

void InstallSignalHandlers()
{
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
}

bool StopRequested()
{
    return g_stop_requested.load(std::memory_order_relaxed);
}

std::string InstrumentTypeToString(std::optional<QTrading::Dto::Trading::InstrumentType> type)
{
    if (!type.has_value()) {
        return "auto";
    }
    switch (*type) {
    case QTrading::Dto::Trading::InstrumentType::Spot:
        return "spot";
    case QTrading::Dto::Trading::InstrumentType::Perp:
        return "perp";
    default:
        break;
    }
    return "unknown";
}

} // namespace QTrading::Service::Helpers
