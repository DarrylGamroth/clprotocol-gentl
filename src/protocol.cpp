// SPDX-License-Identifier: MIT
#include "protocol.hpp"

#include <gentl_producer_core/port_model.hpp>
#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <thread>

namespace clprotocol {
namespace {
using namespace std::chrono;

struct LibraryCloser {
    void operator()(void *library) const noexcept { dlclose(library); }
};

class Library {
public:
    explicit Library(const char *path) : library_(dlopen(path, RTLD_NOW | RTLD_LOCAL)) {
        if (!library_) throw std::runtime_error(dlerror());
    }

    template<class T> T symbol(const char *name, bool required = true) const {
        dlerror();
        auto function = reinterpret_cast<T>(dlsym(library_.get(), name));
        if (const char *error = dlerror(); error && required) throw std::runtime_error(error);
        return function;
    }

private:
    std::unique_ptr<void, LibraryCloser> library_;
};

void check_serial(CLINT32 status, const char *operation)
{
    if (status != CL_ERR_NO_ERR)
        throw ProtocolError(status, std::string(operation) + " returned " + std::to_string(status));
}

std::string checked_string(const char *data, std::size_t size)
{
    const auto length = strnlen(data, size);
    if (!length || length == size) throw std::runtime_error("invalid CLProtocol string");
    return {data, length};
}
} // namespace

class SerialLibrary {
public:
    explicit SerialLibrary(const char *path) : library_(path) {
        if (!count_ports) count_ports = library_.symbol<CountPorts>("clGetNumSerialPorts");
    }

private:
    Library library_;
public:
#define LOAD(name) decltype(&::name) name = library_.symbol<decltype(&::name)>(#name)
    using CountPorts = decltype(&::clGetNumPorts);
    CountPorts count_ports = library_.symbol<CountPorts>("clGetNumPorts", false);
    LOAD(clSerialInit);
    LOAD(clSerialClose);
    LOAD(clSerialRead);
    LOAD(clSerialWrite);
    LOAD(clGetNumBytesAvail);
    LOAD(clFlushPort);
    LOAD(clGetSupportedBaudRates);
    LOAD(clSetBaudRate);
#undef LOAD
};

class CameraConnection::SerialPort final : public ISerial {
public:
    SerialPort(SerialLibrary &library, CLUINT32 channel) : library_(library) {
        check_serial(library_.clSerialInit(channel, &ref_), "clSerialInit");
        if (!ref_) throw std::runtime_error("clSerialInit returned an empty reference");
        auto result = library_.clFlushPort(ref_);
        if (result != CL_ERR_NO_ERR) {
            library_.clSerialClose(ref_);
            check_serial(result, "clFlushPort");
        }
    }
    ~SerialPort() {
        try {
            library_.clSerialClose(ref_);
        } catch (...) {
        }
    }

    CLINT32 CLPROTOCOL clSerialRead(CLINT8 *data, CLUINT32 *size, CLUINT32 timeout) override {
        if (!data || !size) return CL_ERR_INVALID_PTR;
        const auto requested = *size;
        *size = 0;
        if (!requested) return CL_ERR_NO_ERR;
        const auto deadline = steady_clock::now() + milliseconds(timeout);
        for (;;) {
            CLUINT32 available = 0;
            const auto status = library_.clGetNumBytesAvail(ref_, &available);
            if (status != CL_ERR_NO_ERR) return status;
            const auto remaining = ceil<milliseconds>(deadline - steady_clock::now()).count();
            if (available) {
                // Read the queued prefix. Vendor clSerialRead can wait for an entire request.
                const auto count = std::min(requested, available);
                auto received = count;
                const auto result = library_.clSerialRead(ref_, data, &received,
                    static_cast<CLUINT32>(std::max<std::int64_t>(0, remaining)));
                if (received > count) return CL_ERR_INVALID_REFERENCE;
                if (result == CL_ERR_NO_ERR) *size = received;
                return result;
            }
            if (remaining <= 0) return CL_ERR_TIMEOUT;
            std::this_thread::sleep_until(std::min(deadline, steady_clock::now() + milliseconds(1)));
        }
    }

    CLINT32 CLPROTOCOL clSerialWrite(CLINT8 *data, CLUINT32 *size, CLUINT32 timeout) override {
        if (!data || !size) return CL_ERR_INVALID_PTR;
        const auto requested = *size;
        const auto status = library_.clSerialWrite(ref_, data, size, timeout);
        if (*size > requested) return CL_ERR_INVALID_REFERENCE;
        return status;
    }

    CLINT32 CLPROTOCOL clGetSupportedBaudRates(CLUINT32 *rates) override {
        if (!rates) return CL_ERR_INVALID_PTR;
        return library_.clGetSupportedBaudRates(ref_, rates);
    }

    CLINT32 CLPROTOCOL clSetBaudRate(CLUINT32 rate) override {
        return library_.clSetBaudRate(ref_, rate);
    }

private:
    SerialLibrary &library_;
    hSerRef ref_ = nullptr;
};

class ProtocolLibrary {
public:
    explicit ProtocolLibrary(Library library) : library_(std::move(library)) {
        const auto status = clpInitLib(nullptr, CLP_LOG_WARN);
        check(status, 0, "clpInitLib");
        try {
            CLUINT32 major = 0, minor = 0;
            check(clpGetCLProtocolVersion(&major, &minor), 0, "clpGetCLProtocolVersion");
            if (major != 1) throw std::runtime_error("expected CLProtocol ABI major version 1");
        } catch (...) {
            close();
            throw;
        }
    }
    ~ProtocolLibrary() { close(); }

    void disconnect(CLUINT32 cookie) noexcept {
        try {
            clpDisconnect(cookie);
        } catch (...) {
        }
    }

    std::vector<std::string> templates() {
        CLUINT32 size = 0;
        auto status = clpGetShortDeviceIDTemplates(nullptr, &size);
        if (status != CL_ERR_BUFFER_TOO_SMALL)
            check(status, 0, "clpGetShortDeviceIDTemplates");
        if (!size || size > 65536) throw std::runtime_error("invalid CLProtocol template list size");
        std::vector<char> data(size);
        check(clpGetShortDeviceIDTemplates(data.data(), &size), 0, "clpGetShortDeviceIDTemplates");
        const auto text = checked_string(data.data(), std::min<std::size_t>(size, data.size()));
        std::vector<std::string> result;
        for (auto advertised : split(text, '\t'))
            if (!advertised.empty()) result.emplace_back(advertised);
        return result;
    }

    bool advertises(const std::string &selected) {
        const auto advertised = templates();
        return std::any_of(advertised.begin(), advertised.end(), [&](const auto &entry) {
            return matches_template(selected, entry) || matches_template(entry, selected);
        });
    }

    void check(CLINT32 status, CLUINT32 cookie, const char *operation) {
        if (status == CL_ERR_NO_ERR) return;
        std::array<CLINT8, 512> buffer{};
        CLUINT32 size = buffer.size();
        const auto result = clpGetErrorText(status, buffer.data(), &size, cookie);
        const std::string explanation = result == CL_ERR_NO_ERR
            ? std::string(buffer.data(), strnlen(buffer.data(), buffer.size()))
            : std::to_string(status);
        throw ProtocolError(status, std::string(operation) + " failed: " + explanation);
    }

private:
    void close() noexcept {
        try {
            clpCloseLib();
        } catch (...) {
        }
    }

    Library library_;
public:
#define LOAD(name) decltype(&::name) name = library_.symbol<decltype(&::name)>(#name)
    LOAD(clpInitLib);
    LOAD(clpCloseLib);
    LOAD(clpProbeDevice);
    LOAD(clpGetShortDeviceIDTemplates);
    LOAD(clpGetXMLIDs);
    LOAD(clpGetXMLDescription);
    LOAD(clpReadRegister);
    LOAD(clpWriteRegister);
    LOAD(clpContinueWriteRegister);
    LOAD(clpDisconnect);
    LOAD(clpGetErrorText);
    LOAD(clpGetCLProtocolVersion);
#undef LOAD
};

ProtocolError::ProtocolError(CLINT32 code, const std::string &message)
    : std::runtime_error(message), status(code) {}

namespace {

std::unique_ptr<SerialLibrary> open_serial_library(const char *override_path)
{
    if (override_path && *override_path) return std::make_unique<SerialLibrary>(override_path);

    constexpr char filename[] = "libCLAllSerial_gcc8_v3_5.so";
    for (const auto &root : registration_roots()) {
        const auto path = root / platform_directory / filename;
        if (std::filesystem::is_regular_file(path)) return std::make_unique<SerialLibrary>(path.c_str());
    }
    const auto adjacent = gentl_producer_core::cti_runtime_directory() / filename;
    if (std::filesystem::is_regular_file(adjacent)) return std::make_unique<SerialLibrary>(adjacent.c_str());
    return std::make_unique<SerialLibrary>(filename);
}

} // namespace

Libraries::Libraries(const char *serial_path, const char *config_path)
    : serial_(open_serial_library(serial_path))
{
    if (config_path && *config_path) {
        ports_ = read_config(config_path);
        for (const auto &entry : ports_) {
            const auto &port = entry.second;
            auto &protocol = protocols_[port.module];
            if (!protocol) protocol = std::make_unique<ProtocolLibrary>(Library(port.library.c_str()));
            if (!protocol->advertises(port.device_template))
                throw std::runtime_error(port.library.string() + " does not advertise template " +
                                         port.device_template);
        }
        return;
    }

    const char *selection = std::getenv("CLPROTOCOL_GENTL_DEVICE_TEMPLATE");
    for (const auto &[module, path] : registered_drivers()) {
        Library library(path.c_str());
        using GetTemplates = decltype(&::clpGetShortDeviceIDTemplates);
        if (!library.symbol<GetTemplates>("clpGetShortDeviceIDTemplates", false)) continue;
        auto protocol = std::make_unique<ProtocolLibrary>(std::move(library));
        for (const auto &device_template : protocol->templates()) {
            if (selection && *selection && device_template != selection) continue;
            if (registered_driver_)
                throw std::runtime_error("multiple CLProtocol device templates match GENICAM_CLPROTOCOL; "
                                         "select an advertised CLPROTOCOL_GENTL_DEVICE_TEMPLATE");
            registered_driver_ = PortConfig{path, module, device_template, "Device", true};
        }
        if (registered_driver_ && registered_driver_->module == module)
            protocols_.emplace(module, std::move(protocol));
    }
    if (!registered_driver_)
        throw std::runtime_error("no matching CLProtocol device template in GENICAM_CLPROTOCOL");
}

const PortConfig *Libraries::port_config(std::uint32_t channel) const
{
    if (registered_driver_) return &*registered_driver_;
    const auto found = ports_.find(channel);
    return found == ports_.end() ? nullptr : &found->second;
}

Libraries::~Libraries() = default;

std::uint32_t Libraries::port_count()
{
    CLUINT32 count = 0;
    check_serial(serial_->count_ports(&count), "enumerate Camera Link serial ports");
    return count;
}

std::unique_ptr<CameraConnection> Libraries::connect(std::uint32_t channel, std::uint32_t timeout,
                                                      const CameraInfo *known)
{
    const auto *port = port_config(channel);
    if (!port) throw std::out_of_range("no CLProtocol device template for the serial port");
    return std::make_unique<CameraConnection>(*serial_, *protocols_.at(port->module),
                                               channel, *port, known, timeout);
}

namespace {
CameraInfo camera_info(std::uint32_t channel, std::string protocol_id, std::string_view device_template)
{
    std::string_view short_id(protocol_id);
    if (!matches_template(short_id, device_template)) {
        const auto first = short_id.find('#');
        const auto second = first == short_id.npos ? first : short_id.find('#', first + 1);
        if (second == short_id.npos)
            throw ProtocolError(CL_ERR_INVALID_DEVICEID, "malformed CLProtocol DeviceID");
        short_id.remove_prefix(second + 1);
    }
    const auto fields = split(short_id, '#');
    if (!matches_template(short_id, device_template) || fields.size() < 3 || fields.size() > 5 ||
        fields[0].empty() || fields[1].empty() || fields[2].empty())
        throw ProtocolError(CL_ERR_INVALID_DEVICEID, "CLProtocol DeviceID does not match the configured template");

    CameraInfo info{channel, {}, std::string(short_id), std::string(fields[0]),
                    std::string(fields[2]), {}, {}};
    if (fields.size() > 3) info.version = fields[3];
    if (fields.size() > 4) info.serial = fields[4];
    info.protocol_id = std::move(protocol_id);
    return info;
}
} // namespace

CameraConnection::CameraConnection(SerialLibrary &serial, ProtocolLibrary &protocol,
                                   std::uint32_t channel, const PortConfig &port,
                                   const CameraInfo *known, std::uint32_t timeout)
    : serial_(std::make_unique<SerialPort>(serial, channel)), protocol_(protocol)
{
    const auto &selection = known ? known->protocol_id : port.device_template;
    std::vector<CLINT8> id(512);
    CLUINT32 size = static_cast<CLUINT32>(id.size());
    try {
        auto status = protocol_.clpProbeDevice(serial_.get(), selection.c_str(), id.data(), &size,
                                               &cookie_, timeout);
        if (status == CL_ERR_BUFFER_TOO_SMALL && !cookie_ && size > id.size() && size <= 65536) {
            id.resize(size);
            status = protocol_.clpProbeDevice(serial_.get(), selection.c_str(), id.data(), &size,
                                               &cookie_, timeout);
        }
        protocol_.check(status, 0, "clpProbeDevice");
        if (!cookie_ || size > id.size()) throw std::runtime_error("invalid CLProtocol connection");
        identity_ = camera_info(channel, checked_string(id.data(), size), port.device_template);
    } catch (...) {
        if (cookie_) protocol_.disconnect(cookie_);
        throw;
    }
}

CameraConnection::~CameraConnection()
{
    protocol_.disconnect(cookie_);
}

std::vector<std::string> CameraConnection::xml_ids()
{
    CLUINT32 size = 0;
    auto status = protocol_.clpGetXMLIDs(serial_.get(), cookie_, nullptr, &size, control_timeout_ms);
    if (status != CL_ERR_BUFFER_TOO_SMALL)
        protocol_.check(status, cookie_, "clpGetXMLIDs");
    if (!size) return {};
    if (size > 65536) throw std::runtime_error("invalid CLProtocol XML ID list size");
    std::vector<char> ids(size);
    protocol_.check(protocol_.clpGetXMLIDs(serial_.get(), cookie_, ids.data(), &size,
                    control_timeout_ms), cookie_, "clpGetXMLIDs");
    if (!size || size > ids.size() || ids[size - 1] != '\0')
        throw std::runtime_error("invalid CLProtocol XML ID list");
    std::vector<std::string> result;
    for (auto id : split(std::string_view(ids.data(), strnlen(ids.data(), size)), '\t')) {
        if (!id.empty()) result.emplace_back(id);
    }
    return result;
}

std::vector<char> CameraConnection::xml(const std::string &id)
{
    CLUINT32 size = 0;
    auto status = protocol_.clpGetXMLDescription(serial_.get(), cookie_, id.c_str(), nullptr,
                                                 &size, control_timeout_ms);
    if (status != CL_ERR_BUFFER_TOO_SMALL)
        protocol_.check(status, cookie_, "clpGetXMLDescription");
    if (!size || size > max_xml_bytes) throw std::runtime_error("invalid CLProtocol XML size");
    std::vector<char> bytes(size);
    protocol_.check(protocol_.clpGetXMLDescription(serial_.get(), cookie_, id.c_str(), bytes.data(),
                    &size, control_timeout_ms), cookie_, "clpGetXMLDescription");
    if (!size || size > bytes.size()) throw std::runtime_error("invalid CLProtocol XML length");
    bytes.resize(size);
    return bytes;
}

void CameraConnection::read(std::uint64_t address, void *data, std::size_t size, std::uint32_t timeout)
{
    protocol_.check(protocol_.clpReadRegister(serial_.get(), cookie_, static_cast<CLINT64>(address),
        static_cast<CLINT8 *>(data), static_cast<CLINT64>(size), timeout), cookie_, "clpReadRegister");
}

void CameraConnection::write(std::uint64_t address, const void *data, std::size_t size)
{
    auto status = protocol_.clpWriteRegister(serial_.get(), cookie_, static_cast<CLINT64>(address),
        static_cast<const CLINT8 *>(data), static_cast<CLINT64>(size), control_timeout_ms);
    if (status == CL_ERR_PENDING_WRITE) {
        using namespace std::chrono;
        const auto deadline = steady_clock::now() + milliseconds(control_timeout_ms);
        do {
            const auto remaining = ceil<milliseconds>(deadline - steady_clock::now()).count();
            if (remaining <= 0) {
                protocol_.clpContinueWriteRegister(serial_.get(), cookie_, false, control_timeout_ms);
                throw ProtocolError(CL_ERR_TIMEOUT, "CLProtocol write timed out waiting for completion");
            }
            status = protocol_.clpContinueWriteRegister(serial_.get(), cookie_, true,
                                                         static_cast<CLUINT32>(remaining));
            if (status == CL_ERR_PENDING_WRITE)
                std::this_thread::sleep_until(std::min(deadline, steady_clock::now() + milliseconds(1)));
        } while (status == CL_ERR_PENDING_WRITE);
    }
    protocol_.check(status, cookie_, "clpWriteRegister");
}

} // namespace clprotocol
