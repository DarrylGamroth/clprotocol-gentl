// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace clprotocol {

struct PortConfig {
    std::filesystem::path library;
    std::filesystem::path module;
    std::string device_template;
    std::string port_name;
    bool little_endian;
};

std::vector<std::filesystem::path> registration_roots();
std::map<std::filesystem::path, std::filesystem::path> registered_drivers();
std::map<std::uint32_t, PortConfig> read_config(const std::filesystem::path &file);
std::vector<std::string_view> split(std::string_view value, char separator);
bool matches_template(std::string_view id, std::string_view device_template);

} // namespace clprotocol
