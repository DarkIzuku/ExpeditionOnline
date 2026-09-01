#pragma once

#include <cstdint>
#include <string>

#ifndef EXPEDITION_BUILD_COMMIT
#define EXPEDITION_BUILD_COMMIT "unknown"
#endif

namespace expedition_online::build_info
{
inline constexpr const char* kVersion = "0.3.0-rc2";
inline constexpr const char* kCommit = EXPEDITION_BUILD_COMMIT;

inline auto identity(const char* component, std::uint16_t protocol_version) -> std::string
{
    return std::string("ExpeditionOnline ") + component + ' ' + kVersion + " Protocol " +
           std::to_string(protocol_version) + " Build commit " + kCommit;
}
} // namespace expedition_online::build_info
