// SPDX-License-Identifier: MIT
#pragma once

#include "protocol.hpp"

#include <gentl_producer_core/port_model.hpp>

namespace clprotocol {

class XmlFiles {
public:
    XmlFiles(CameraConnection &camera, const PortConfig &port);
    ~XmlFiles();
    XmlFiles(const XmlFiles &) = delete;
    XmlFiles &operator=(const XmlFiles &) = delete;
    std::vector<gentl_producer_core::PortUrlInfo> take_urls() { return std::move(urls_); }

private:
    std::filesystem::path directory_;
    std::vector<gentl_producer_core::PortUrlInfo> urls_;
};

} // namespace clprotocol
