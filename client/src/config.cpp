#include <expedition_online/client/config.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace expedition_online::client
{
namespace
{
auto trim(std::string value) -> std::string
{
    const auto not_space = [](unsigned char c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n'; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}
} // namespace

auto load_client_config(const std::filesystem::path& path) -> ClientConfig
{
    ClientConfig config;
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error("cannot open client config: " + path.string());
    }

    std::string section;
    std::string line;
    while (std::getline(input, line))
    {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line.front() == '[' && line.back() == ']')
        {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));

        if (section == "companions")
        {
            if (!key.empty() && !value.empty()) config.companion_by_character[key] = value;
            continue;
        }

        if (key == "host" || key == "ServerHost") config.host = value;
        else if (key == "port" || key == "ServerPort")
        {
            const auto parsed = std::stoi(value);
            if (parsed < 1 || parsed > 65535) throw std::runtime_error("port must be in 1..65535");
            config.port = static_cast<std::uint16_t>(parsed);
        }
        else if (key == "player_name" || key == "PlayerName") config.player_name = value;
        else if (key == "reconnect_delay_ms") config.reconnect_delay_ms = std::clamp(std::stoi(value), 250, 60000);
        else if (key == "snapshot_hz") config.snapshot_hz = std::clamp(std::stoi(value), 1, 60);
        else if (key == "controller_class") config.controller_class = value;
        else if (key == "pawn_property") config.pawn_property = value;
        else if (key == "body_component_property") config.body_component_property = value;
        else if (key == "hair_component_property") config.hair_component_property = value;
        else if (key == "mesh_asset_property") config.mesh_asset_property = value;
        else if (key == "controller_property") config.controller_property = value;
        else if (key == "brain_component_property") config.brain_component_property = value;
        else if (key == "default_companion_class") config.default_companion_class = value;
    }
    return config;
}
} // namespace expedition_online::client
