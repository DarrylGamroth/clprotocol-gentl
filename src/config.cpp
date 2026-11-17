// SPDX-License-Identifier: MIT
#include "config.hpp"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace clprotocol {
namespace fs = std::filesystem;
namespace {

fs::path resolve_library(const fs::path &library)
{
    if (library.is_absolute()) return library.lexically_normal();
    if (library.has_parent_path())
        throw std::runtime_error("CLProtocol library must be an absolute path or registered filename");

    std::map<fs::path, fs::path> matches;
    for (const auto &root : registration_roots()) {
        const auto file = root / platform_directory / library;
        if (fs::is_regular_file(file)) matches.emplace(fs::canonical(file), file);
    }
    if (matches.size() != 1)
        throw std::runtime_error("expected one registered CLProtocol library named " +
                                 library.string() + ", found " + std::to_string(matches.size()));
    return matches.begin()->second;
}

} // namespace

std::vector<std::string_view> split(std::string_view value, char separator)
{
    std::vector<std::string_view> fields;
    for (;;) {
        const auto end = value.find(separator);
        fields.push_back(value.substr(0, end));
        if (end == value.npos) return fields;
        value.remove_prefix(end + 1);
    }
}

std::vector<fs::path> registration_roots()
{
    std::vector<fs::path> roots;
    if (const char *registered = std::getenv("GENICAM_CLPROTOCOL")) {
        std::string paths(registered);
        for (auto &c : paths)
            if (c == ';') c = ':';
        for (auto root : split(paths, ':'))
            if (!root.empty()) roots.push_back(fs::absolute(fs::path(root)));
    }
    return roots;
}

std::map<fs::path, fs::path> registered_drivers()
{
    std::map<fs::path, fs::path> drivers;
    for (const auto &root : registration_roots()) {
        const auto directory = root / platform_directory;
        if (!fs::is_directory(directory)) continue;
        for (const auto &entry : fs::directory_iterator(directory)) {
            const auto name = entry.path().filename().string();
            if (!name.starts_with("lib") || !name.ends_with(".so") ||
                name.starts_with("libCLAllSerial") || !entry.is_regular_file()) continue;
            if (name.ends_with(".debug.so") &&
                fs::is_regular_file(directory / (name.substr(0, name.size() - 9) + ".so"))) continue;
            drivers.emplace(fs::canonical(entry.path()), entry.path());
        }
    }
    return drivers;
}

bool matches_template(std::string_view id, std::string_view device_template)
{
    return id.substr(0, device_template.size()) == device_template &&
           (id.size() == device_template.size() || id[device_template.size()] == '#');
}

std::map<std::uint32_t, PortConfig> read_config(const fs::path &file)
{
    std::ifstream input(file);
    if (!input) throw std::runtime_error("cannot open port configuration " + file.string());

    std::map<std::uint32_t, PortConfig> ports;
    unsigned line_number = 0;
    std::string line;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto fields = split(line, '\t');
        std::uint32_t index = 0;
        auto result = std::from_chars(fields[0].data(), fields[0].data() + fields[0].size(), index);
        const auto invalid = [&] {
            return std::runtime_error(file.string() + ":" + std::to_string(line_number) +
                                      ": invalid CLProtocol port assignment");
        };
        if (fields.size() < 3 || fields.size() > 5 || fields[1].empty() || fields[2].empty() ||
            result.ec != std::errc{} || result.ptr != fields[0].data() + fields[0].size())
            throw invalid();
        const auto port_name = fields.size() >= 4 ? fields[3] : "Device";
        const auto byte_order = fields.size() == 5 ? fields[4] : "little";
        if (port_name.empty() || (byte_order != "little" && byte_order != "big") ||
            ports.count(index))
            throw invalid();
        auto library = resolve_library(fs::path(fields[1]));
        auto module = fs::canonical(library);
        ports.emplace(index, PortConfig{std::move(library), std::move(module), std::string(fields[2]),
                                       std::string(port_name), byte_order == "little"});
    }
    if (input.bad()) throw std::runtime_error("cannot read port configuration " + file.string());
    if (ports.empty()) throw std::runtime_error("port configuration has no assignments");
    return ports;
}

} // namespace clprotocol
