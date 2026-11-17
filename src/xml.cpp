// SPDX-License-Identifier: MIT
#include "xml.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <map>
#include <system_error>
#include <unistd.h>

namespace clprotocol {
namespace fs = std::filesystem;
namespace {

constexpr size_t max_files = 64;
constexpr size_t max_total_bytes = 4 * max_xml_bytes;

std::string file_url(const fs::path &path)
{
    constexpr std::string_view unreserved =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~/";
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result = "file://";
    for (unsigned char c : path.native()) {
        if (unreserved.find(c) != unreserved.npos) {
            result += static_cast<char>(c);
        } else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}

std::string_view without_xml_extension(std::string_view text)
{
    constexpr std::string_view suffix = ".xml";
    if (text.size() > suffix.size() && text.substr(text.size() - suffix.size()) == suffix)
        text.remove_suffix(suffix.size());
    return text;
}

template <size_t N>
void version_numbers(std::string_view text, std::string_view prefix, std::int32_t *output)
{
    if (text.substr(0, prefix.size()) != prefix) return;
    const auto fields = split(text.substr(prefix.size()), '.');
    if (fields.size() != N) return;
    std::array<std::int32_t, N> numbers = {};
    for (size_t i = 0; i < N; ++i) {
        const auto end = fields[i].data() + fields[i].size();
        const auto parse = std::from_chars(fields[i].data(), end, numbers[i]);
        if (parse.ec != std::errc{} || parse.ptr != end || numbers[i] < 0) return;
    }
    std::copy(numbers.begin(), numbers.end(), output);
}

std::array<std::int32_t, 5> xml_version(const std::string &id)
{
    std::array<std::int32_t, 5> result{-1, -1, -1, -1, -1};
    const auto fields = split(without_xml_extension(id), '@');
    if (fields.size() == 3) {
        version_numbers<2>(fields[0], "SchemaVersion.", result.data());
        version_numbers<3>(fields[2], "XMLVersion.", result.data() + 2);
    }
    return result;
}

struct InstalledXml {
    fs::path path;
    size_t specificity;
    std::array<std::int32_t, 5> version;
};

std::map<std::string, InstalledXml> installed_xml(const PortConfig &port, const CameraInfo &identity)
{
    std::vector<fs::path> locations;
    for (const auto &path : {port.library, port.module}) {
        const auto directory = path.parent_path();
        const auto root = directory.filename() == "Linux64_x64" ? directory.parent_path() : directory;
        for (const auto &location : {root, directory})
            if (std::find(locations.begin(), locations.end(), location) == locations.end())
                locations.push_back(location);
    }
    std::map<std::string, InstalledXml> result;
    for (const auto &location : locations) {
        std::error_code error;
        for (fs::directory_iterator it(location, error), end; !error && it != end; it.increment(error)) {
            const auto &path = it->path();
            if (path.extension() != ".xml" || !it->is_regular_file()) continue;
            const auto id = path.stem().string();
            const auto fields = split(id, '@');
            if (fields.size() != 3 || fields[1].empty() ||
                (!matches_template(identity.short_id, fields[1]) &&
                 !matches_template(identity.protocol_id, fields[1]))) continue;
            const auto version = xml_version(id);
            if (version.front() < 0 || version.back() < 0) continue;
            result.emplace(id, InstalledXml{path, fields[1].size(), version});
        }
        if (error && error != std::errc::permission_denied)
            throw std::system_error(error, "scan CLProtocol XML directory");
    }
    return result;
}

std::vector<char> read_file(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    if (length <= 0 || length > static_cast<std::streamoff>(max_xml_bytes))
        throw std::runtime_error("invalid CLProtocol XML file size for " + path.string());
    std::vector<char> bytes(static_cast<size_t>(length));
    if (!input.seekg(0) || !input.read(bytes.data(), length))
        throw std::runtime_error("cannot read XML from " + path.string());
    return bytes;
}

fs::path private_directory()
{
    auto pattern = (fs::absolute(fs::temp_directory_path()) / "clprotocol-gentl-XXXXXX").string();
    const char *result = mkdtemp(pattern.data());
    if (!result) throw std::system_error(errno, std::generic_category(), "create CLProtocol XML directory");
    try {
        return fs::path(result);
    } catch (...) {
        ::rmdir(result);
        throw;
    }
}

void remove_directory(const fs::path &directory) noexcept
{
    try {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
    } catch (...) {
    }
}
} // namespace

XmlFiles::XmlFiles(CameraConnection &camera, const PortConfig &port) : directory_(private_directory())
{
    try {
        auto installed = installed_xml(port, camera.identity());
        size_t total_bytes = 0;
        const auto add = [&](const std::string &id, const std::vector<char> &bytes) {
            if (urls_.size() >= max_files || bytes.size() > max_total_bytes - total_bytes)
                throw std::runtime_error("CLProtocol XML bundle exceeds the size limit");
            const bool zipped = bytes.size() >= 4 &&
                std::equal(bytes.begin(), bytes.begin() + 4, "PK\x03\x04");
            const auto file = directory_ / (std::to_string(urls_.size()) + (zipped ? ".zip" : ".xml"));
            std::ofstream output(file, std::ios::binary);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            output.close();
            if (!output) throw std::runtime_error("cannot publish CLProtocol XML file");
            const auto version = xml_version(id);
            urls_.push_back({.url = file_url(file),
                             .filename = file.string(),
                             .schema_major = version[0],
                             .schema_minor = version[1],
                             .file_major = version[2],
                             .file_minor = version[3],
                             .file_subminor = version[4],
                             .file_size = bytes.size(),
                             .scheme = GenTL::URL_SCHEME_FILE});
            total_bytes += bytes.size();
        };

        std::exception_ptr first_failure;
        for (const auto &id : camera.xml_ids()) {
            const auto entry = installed.find(std::string(without_xml_extension(id)));
            try {
                add(id, camera.xml(id));
            } catch (const ProtocolError &) {
                if (!first_failure) first_failure = std::current_exception();
                if (entry == installed.end()) continue;
                add(id, read_file(entry->second.path));
            }
            if (entry != installed.end()) installed.erase(entry);
        }
        // Keep the driver's preference order, then offer matching installed XML.
        std::vector<std::pair<std::string, InstalledXml>> candidates(installed.begin(), installed.end());
        std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
            if (a.second.version != b.second.version) return a.second.version > b.second.version;
            return a.second.specificity > b.second.specificity;
        });
        for (const auto &entry : candidates) add(entry.first, read_file(entry.second.path));
        if (urls_.empty()) {
            if (first_failure) std::rethrow_exception(first_failure);
            throw ProtocolError(CL_ERR_NO_XMLDESCRIPTION_FOUND, "no CLProtocol XML for " + camera.identity().short_id);
        }
    } catch (...) {
        remove_directory(directory_);
        throw;
    }
}

XmlFiles::~XmlFiles()
{
    remove_directory(directory_);
}

} // namespace clprotocol
