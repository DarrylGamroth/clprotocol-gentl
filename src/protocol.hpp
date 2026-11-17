// SPDX-License-Identifier: MIT
#pragma once

#include "config.hpp"

#include <CLProtocol/CLProtocol.h>
#include <CLProtocol/ClSerial.h>
#include <CLProtocol/ISerial.h>

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace clprotocol {

class SerialLibrary;
class ProtocolLibrary;

struct CameraInfo {
    std::uint32_t channel;
    std::string protocol_id;
    std::string short_id;
    std::string manufacturer;
    std::string model;
    std::string version;
    std::string serial;

    bool operator==(const CameraInfo &other) const {
        return channel == other.channel && protocol_id == other.protocol_id;
    }
};

class ProtocolError : public std::runtime_error {
public:
    ProtocolError(CLINT32 status, const std::string &message);
    CLINT32 status;
};

class CameraConnection {
public:
    CameraConnection(SerialLibrary &serial, ProtocolLibrary &protocol, std::uint32_t channel,
                     const PortConfig &port, const CameraInfo *known, std::uint32_t timeout);
    ~CameraConnection();

    const CameraInfo &identity() const { return identity_; }
    std::vector<std::string> xml_ids();
    std::vector<char> xml(const std::string &id);
    void read(std::uint64_t address, void *data, std::size_t size, std::uint32_t timeout);
    void write(std::uint64_t address, const void *data, std::size_t size);

private:
    class SerialPort;
    std::unique_ptr<SerialPort> serial_;
    ProtocolLibrary &protocol_;
    CLUINT32 cookie_ = 0;
    CameraInfo identity_;
};

class Libraries {
public:
    Libraries(const char *serial_path, const char *config_path);
    ~Libraries();
    std::uint32_t port_count();
    const std::map<std::uint32_t, PortConfig> &ports() const { return ports_; }
    std::unique_ptr<CameraConnection> connect(std::uint32_t channel, std::uint32_t timeout,
                                               const CameraInfo *known = nullptr);

private:
    std::unique_ptr<SerialLibrary> serial_;
    std::map<std::uint32_t, PortConfig> ports_;
    std::map<std::filesystem::path, std::unique_ptr<ProtocolLibrary>> protocols_;
};

inline constexpr std::uint32_t control_timeout_ms = 1000;
inline constexpr std::size_t max_xml_bytes = 16 * 1024 * 1024;

} // namespace clprotocol
