// SPDX-License-Identifier: MIT
#pragma once

#include "protocol.hpp"

#include <array>

namespace clprotocol {

struct XmlFile {
    std::string filename;
    std::string url;
    std::uint64_t size;
    // Schema major/minor followed by document major/minor/subminor.
    std::array<std::int32_t, 5> version;
};

class XmlFiles {
public:
    XmlFiles(CameraConnection &camera, const PortConfig &port);
    ~XmlFiles();
    XmlFiles(const XmlFiles &) = delete;
    XmlFiles &operator=(const XmlFiles &) = delete;
    const std::vector<XmlFile> &files() const { return files_; }

private:
    std::filesystem::path directory_;
    std::vector<XmlFile> files_;
};

} // namespace clprotocol
