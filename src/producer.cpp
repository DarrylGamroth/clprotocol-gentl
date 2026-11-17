// SPDX-License-Identifier: MIT
#include "xml.hpp"

#include <GenTL.h>
#include <gentl_producer_core/error.hpp>
#include <gentl_producer_core/info.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <string_view>

namespace GenTL {
namespace {
using namespace clprotocol;
namespace core = gentl_producer_core;

constexpr char interface_id[] = "CameraLinkSerial";
constexpr auto max_register_address = static_cast<uint64_t>(std::numeric_limits<CLINT64>::max());

struct Device {
    CameraInfo identity;
    std::unique_ptr<CameraConnection> connection;
    std::unique_ptr<XmlFiles> xml;
    core::PortModel port;
};

struct Producer {
    std::unique_ptr<Libraries> libraries;
    std::vector<CameraInfo> discovered;
    std::map<uint32_t, std::unique_ptr<Device>> open;
    char system_handle, interface_handle;
    bool system_open = false;
    bool interface_open = false;
} producer;

std::mutex mutex;

struct Failure { GC_ERROR code; const char *text; };

void require(bool condition, GC_ERROR code, const char *text)
{
    if (!condition) throw Failure{code, text};
}

void check(GC_ERROR code, const char *text)
{
    require(code == GC_ERR_SUCCESS, code, text);
}

GC_ERROR error(GC_ERROR code, const char *text) noexcept
{
    try {
        core::set_last_error(code, std::string(text, strnlen(text, 511)));
    } catch (...) {
        // The ABI must still return the original error if storing its text fails.
        try { core::set_last_error(code, {}); } catch (...) {}
    }
    return code;
}

template<class F> GC_ERROR call(F action) noexcept
{
    try {
        const std::lock_guard lock(mutex);
        action();
        return GC_ERR_SUCCESS;
    } catch (const Failure &failure) {
        return error(failure.code, failure.text);
    } catch (const ProtocolError &failure) {
        switch (failure.status) {
        case CL_ERR_TIMEOUT: return error(GC_ERR_TIMEOUT, failure.what());
        case CL_ERR_OUT_OF_MEMORY: return error(GC_ERR_OUT_OF_MEMORY, failure.what());
        case CL_ERR_PARAM_READ_ONLY: return error(GC_ERR_ACCESS_DENIED, failure.what());
        case CL_ERR_NO_XMLDESCRIPTION_FOUND: return error(GC_ERR_NOT_AVAILABLE, failure.what());
        case CL_ERR_PORT_IN_USE:
        case CL_ERR_IN_USE: return error(GC_ERR_RESOURCE_IN_USE, failure.what());
        case CL_ERR_NO_DEVICE_FOUND:
        case CL_ERR_INVALID_DEVICEID: return error(GC_ERR_INVALID_ID, failure.what());
        default: return error(GC_ERR_IO, failure.what());
        }
    } catch (const std::bad_alloc &failure) {
        return error(GC_ERR_OUT_OF_MEMORY, failure.what());
    } catch (const std::exception &failure) {
        return error(GC_ERR_ERROR, failure.what());
    } catch (...) {
        return error(GC_ERR_ERROR, "unknown CLProtocol producer failure");
    }
}

void initialize_check()
{
    require(bool(producer.libraries), GC_ERR_NOT_INITIALIZED, "CTI has not been initialized");
}

void check_system(TL_HANDLE handle)
{
    initialize_check();
    require(producer.system_open && handle == &producer.system_handle,
            GC_ERR_INVALID_HANDLE, "invalid system handle");
}

void check_interface(IF_HANDLE handle)
{
    initialize_check();
    require(producer.interface_open && handle == &producer.interface_handle,
            GC_ERR_INVALID_HANDLE, "invalid interface handle");
}

void check_interface_id(const char *id)
{
    require(id, GC_ERR_INVALID_PARAMETER, "missing interface ID");
    require(std::string_view(id) == interface_id, GC_ERR_INVALID_ID, "unknown interface ID");
}

Device &device_for(void *handle, bool remote_port = false)
{
    initialize_check();
    for (const auto &entry : producer.open) {
        auto *device = entry.second.get();
        void *expected = remote_port ? static_cast<void *>(&device->port) : device;
        if (handle == expected) return *device;
    }
    throw Failure{GC_ERR_INVALID_HANDLE, remote_port ? "invalid remote port handle" : "invalid device handle"};
}

std::string device_id(const CameraInfo &info)
{
    return "CLSerial-" + std::to_string(info.channel);
}

const CameraInfo &find_camera(const char *id)
{
    require(id, GC_ERR_INVALID_PARAMETER, "missing device ID");
    const auto found = std::find_if(producer.discovered.begin(), producer.discovered.end(),
        [&](const auto &camera) { return device_id(camera) == id; });
    require(found != producer.discovered.end(), GC_ERR_INVALID_ID, "unknown device ID");
    return *found;
}

void string(std::string_view value, void *output, size_t *size)
{
    check(core::copy_string(value, INFO_DATATYPE_STRING, INFO_DATATYPE_STRING, output, size),
          "cannot copy string");
}

struct Info {
    INFO_DATATYPE *type;
    void *output;
    size_t *size;

    void text(std::string_view value) const {
        INFO_DATATYPE ignored;
        check(core::info_string(value, type ? type : &ignored, output, size),
              "cannot copy string information");
    }
    template<class T> void number(INFO_DATATYPE kind, T value) const {
        INFO_DATATYPE ignored;
        check(core::info_scalar(value, kind, type ? type : &ignored, output, size),
              "cannot copy scalar information");
    }
    void flag(bool value) const { number(INFO_DATATYPE_BOOL8, static_cast<bool8_t>(value)); }
};

void system_info(TL_INFO_CMD command, const Info &out)
{
    switch (command) {
    case TL_INFO_ID: return out.text("CLProtocol-GenTL");
    case TL_INFO_VENDOR: return out.text("CLProtocol GenTL");
    case TL_INFO_MODEL:
    case TL_INFO_DISPLAYNAME: return out.text("CLProtocol Camera Link serial");
    case TL_INFO_VERSION: return out.text("0.1.0");
    case TL_INFO_TLTYPE: return out.text(TLTypeCLName);
    case TL_INFO_NAME:
    case TL_INFO_PATHNAME: {
        const auto path = core::cti_runtime_path("clprotocol-gentl.cti");
        return out.text(command == TL_INFO_NAME ? path.filename().string() : path.string());
    }
    case TL_INFO_CHAR_ENCODING: return out.number(INFO_DATATYPE_INT32, int32_t{TL_CHAR_ENCODING_UTF8});
    case TL_INFO_GENTL_VER_MAJOR: return out.number(INFO_DATATYPE_UINT32, uint32_t{1});
    case TL_INFO_GENTL_VER_MINOR: return out.number(INFO_DATATYPE_UINT32, uint32_t{6});
    default: throw Failure{GC_ERR_INVALID_PARAMETER, "unknown system info command"};
    }
}

void interface_info(INTERFACE_INFO_CMD command, const Info &out)
{
    switch (command) {
    case INTERFACE_INFO_ID: return out.text(interface_id);
    case INTERFACE_INFO_DISPLAYNAME: return out.text("Camera Link serial");
    case INTERFACE_INFO_TLTYPE: return out.text(TLTypeCLName);
    default: throw Failure{GC_ERR_INVALID_PARAMETER, "unknown interface info command"};
    }
}

void device_info(const CameraInfo &camera, DEVICE_INFO_CMD command, const Info &out)
{
    switch (command) {
    case DEVICE_INFO_ID: return out.text(device_id(camera));
    case DEVICE_INFO_VENDOR: return out.text(camera.manufacturer);
    case DEVICE_INFO_MODEL: return out.text(camera.model);
    case DEVICE_INFO_TLTYPE: return out.text(TLTypeCLName);
    case DEVICE_INFO_DISPLAYNAME: return out.text(camera.model + " (" + device_id(camera) + ")");
    case DEVICE_INFO_SERIAL_NUMBER:
        require(!camera.serial.empty(), GC_ERR_NOT_AVAILABLE, "CLProtocol DeviceID has no serial token");
        return out.text(camera.serial);
    case DEVICE_INFO_VERSION:
        require(!camera.version.empty(), GC_ERR_NOT_AVAILABLE, "CLProtocol DeviceID has no version token");
        return out.text(camera.version);
    case DEVICE_INFO_ACCESS_STATUS: {
        auto opened = producer.open.find(camera.channel);
        auto status = DEVICE_ACCESS_STATUS_READWRITE;
        if (opened != producer.open.end())
            status = opened->second->port.descriptor().write_access ? DEVICE_ACCESS_STATUS_OPEN_READWRITE
                                              : DEVICE_ACCESS_STATUS_OPEN_READONLY;
        return out.number(INFO_DATATYPE_INT32, static_cast<int32_t>(status));
    }
    default: throw Failure{GC_ERR_NOT_AVAILABLE, "device info unavailable"};
    }
}

void refresh_devices(bool8_t *changed, uint64_t timeout)
{
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + milliseconds(
        std::min<uint64_t>(timeout, std::numeric_limits<uint32_t>::max()));
    std::vector<CameraInfo> found;
    const auto count = producer.libraries->port_count();
    for (uint32_t channel = 0; channel < count; ++channel) {
        if (!producer.libraries->port_config(channel)) continue;
        auto opened = producer.open.find(channel);
        if (opened != producer.open.end()) {
            found.push_back(opened->second->identity);
            continue;
        }
        const auto probe_timeout = [&] {
            if (timeout == GENTL_INFINITE) return control_timeout_ms;
            const auto remaining = ceil<milliseconds>(deadline - steady_clock::now()).count();
            return static_cast<uint32_t>(std::clamp<int64_t>(remaining, 0, control_timeout_ms));
        };
        if (!probe_timeout()) {
            // Keep unprobed devices in the snapshot when the caller's budget expires.
            const auto previous = std::find_if(producer.discovered.begin(), producer.discovered.end(),
                [&](const auto &camera) { return camera.channel == channel; });
            if (previous != producer.discovered.end()) found.push_back(*previous);
            continue;
        }
        try {
            auto connection = producer.libraries->connect(channel, probe_timeout());
            found.push_back(connection->identity());
        } catch (const ProtocolError &failure) {
            switch (failure.status) {
            case CL_ERR_NO_DEVICE_FOUND:
            case CL_ERR_INVALID_DEVICEID:
            case CL_ERR_INVALID_INDEX:
            case CL_ERR_TIMEOUT:
            case CL_ERR_PORT_IN_USE:
            case CL_ERR_BAUD_RATE_NOT_SUPPORTED: break;
            default: throw;
            }
        }
    }
    if (changed) *changed = found != producer.discovered;
    producer.discovered = std::move(found);
}

void close_interface()
{
    producer.open.clear();
    producer.discovered.clear();
    producer.interface_open = false;
}

core::PortModel make_port(const CameraInfo &camera, CameraConnection &connection,
                          const PortConfig &config, XmlFiles &xml, bool writable)
{
    auto descriptor = core::make_descriptor(device_id(camera), camera.manufacturer, camera.model,
                                            TLTypeCLName, TLRemoteDeviceModuleName, config.port_name,
                                            "1.0", writable);
    descriptor.little_endian = config.little_endian;

    std::vector<core::PortModel::RegisterRegion> registers;
    registers.push_back({
        .address = 0,
        .data = {},
        .size = max_register_address + 1,
        .readable = true,
        .writable = writable,
        .access = [&connection](uint64_t address, std::span<std::byte> bytes, bool write) {
            if (!bytes.empty()) {
                if (write)
                    connection.write(address, bytes.data(), bytes.size());
                else
                    connection.read(address, bytes.data(), bytes.size(), control_timeout_ms);
            }
            return GC_ERR_SUCCESS;
        },
    });
    return {std::move(descriptor), xml.take_urls(), std::move(registers)};
}

void check_transfer(uint64_t address, const void *buffer, size_t *size)
{
    require(size, GC_ERR_INVALID_PARAMETER, "missing transfer size");
    if (!buffer && *size) {
        *size = 0;
        throw Failure{GC_ERR_INVALID_PARAMETER, "missing transfer buffer"};
    }
    // The CLProtocol ABI takes both address and length as signed 64-bit values.
    if (address > max_register_address || *size > max_register_address) {
        *size = 0;
        throw Failure{GC_ERR_INVALID_ADDRESS, "register extent exceeds the CLProtocol address range"};
    }
}

void read_port(Device &camera, uint64_t address, void *buffer, size_t *size)
{
    check_transfer(address, buffer, size);
    std::byte empty{};
    check(camera.port.read(address, buffer ? buffer : &empty, size), "CLProtocol register read failed");
}

void write_port(Device &camera, uint64_t address, const void *buffer, size_t *size)
{
    check_transfer(address, buffer, size);
    const std::byte empty{};
    check(camera.port.write(address, buffer ? buffer : &empty, size), "CLProtocol register write failed");
}
} // namespace

GC_ERROR unsupported_control_operation() noexcept
{
    return call([] {
        initialize_check();
        throw Failure{GC_ERR_NOT_IMPLEMENTED, "CLProtocol serial control provides no streams or events"};
    });
}

GC_API GCInitLib()
{
    return call([&] {
        require(!producer.libraries, GC_ERR_RESOURCE_IN_USE, "CTI already initialized");
        const auto *serial = std::getenv("CLPROTOCOL_GENTL_SERIAL");
        const auto *config = std::getenv("CLPROTOCOL_GENTL_CONFIG");
        producer.libraries = std::make_unique<Libraries>(serial, config);
    });
}

GC_API GCCloseLib()
{
    return call([&] {
        initialize_check();
        close_interface();
        producer.system_open = false;
        producer.libraries.reset();
    });
}

GC_API GCGetLastError(GC_ERROR *code, char *output, size_t *size)
{
    if (!size && (output || !code)) return GC_ERR_INVALID_PARAMETER;
    try {
        const auto [error_code, message] = core::last_error();
        if (code) *code = error_code;
        return size ? core::copy_string(message, INFO_DATATYPE_STRING, INFO_DATATYPE_STRING,
                                        output, size) : GC_ERR_SUCCESS;
    } catch (const std::bad_alloc &) {
        return GC_ERR_OUT_OF_MEMORY;
    } catch (...) {
        return GC_ERR_ERROR;
    }
}

GC_API GCGetInfo(TL_INFO_CMD command, INFO_DATATYPE *type, void *output, size_t *size)
{
    return call([&] { system_info(command, {type, output, size}); });
}

GC_API TLOpen(TL_HANDLE *handle)
{
    return call([&] {
        initialize_check();
        require(handle, GC_ERR_INVALID_PARAMETER, "missing system output");
        *handle = nullptr;
        require(!producer.system_open, GC_ERR_RESOURCE_IN_USE, "system already open");
        producer.system_open = true;
        *handle = &producer.system_handle;
    });
}

GC_API TLClose(TL_HANDLE handle)
{
    return call([&] {
        check_system(handle);
        close_interface();
        producer.system_open = false;
    });
}

GC_API TLGetInfo(TL_HANDLE handle, TL_INFO_CMD command, INFO_DATATYPE *type, void *output, size_t *size)
{
    return call([&] { check_system(handle); system_info(command, {type, output, size}); });
}

GC_API TLUpdateInterfaceList(TL_HANDLE handle, bool8_t *changed, uint64_t)
{
    return call([&] { check_system(handle); if (changed) *changed = false; });
}

GC_API TLGetNumInterfaces(TL_HANDLE handle, uint32_t *count)
{
    return call([&] {
        check_system(handle);
        require(count, GC_ERR_INVALID_PARAMETER, "missing interface count output");
        *count = 1;
    });
}

GC_API TLGetInterfaceID(TL_HANDLE handle, uint32_t index, char *id, size_t *size)
{
    return call([&] {
        check_system(handle);
        require(index == 0, GC_ERR_INVALID_INDEX, "no such interface");
        string(interface_id, id, size);
    });
}

GC_API TLGetInterfaceInfo(TL_HANDLE handle, const char *id, INTERFACE_INFO_CMD command,
                         INFO_DATATYPE *type, void *output, size_t *size)
{
    return call([&] {
        check_system(handle);
        check_interface_id(id);
        interface_info(command, {type, output, size});
    });
}

GC_API TLOpenInterface(TL_HANDLE handle, const char *id, IF_HANDLE *interface)
{
    return call([&] {
        check_system(handle);
        require(interface, GC_ERR_INVALID_PARAMETER, "missing interface output");
        *interface = nullptr;
        check_interface_id(id);
        require(!producer.interface_open, GC_ERR_RESOURCE_IN_USE, "interface already open");
        producer.interface_open = true;
        *interface = &producer.interface_handle;
    });
}

GC_API IFClose(IF_HANDLE handle)
{
    return call([&] {
        check_interface(handle);
        close_interface();
    });
}

GC_API IFGetParentTL(IF_HANDLE handle, TL_HANDLE *system)
{
    return call([&] {
        check_interface(handle);
        require(system, GC_ERR_INVALID_PARAMETER, "missing parent output");
        *system = &producer.system_handle;
    });
}

GC_API IFGetInfo(IF_HANDLE handle, INTERFACE_INFO_CMD command, INFO_DATATYPE *type,
                 void *output, size_t *size)
{
    return call([&] { check_interface(handle); interface_info(command, {type, output, size}); });
}

GC_API IFUpdateDeviceList(IF_HANDLE handle, bool8_t *changed, uint64_t timeout)
{
    return call([&] { check_interface(handle); refresh_devices(changed, timeout); });
}

GC_API IFGetNumDevices(IF_HANDLE handle, uint32_t *count)
{
    return call([&] {
        check_interface(handle);
        require(count, GC_ERR_INVALID_PARAMETER, "missing device count output");
        *count = static_cast<uint32_t>(producer.discovered.size());
    });
}

GC_API IFGetDeviceID(IF_HANDLE handle, uint32_t index, char *id, size_t *size)
{
    return call([&] {
        check_interface(handle);
        require(index < producer.discovered.size(), GC_ERR_INVALID_INDEX, "no such device index");
        string(device_id(producer.discovered[index]), id, size);
    });
}

GC_API IFGetDeviceInfo(IF_HANDLE handle, const char *id, DEVICE_INFO_CMD command,
                     INFO_DATATYPE *type, void *output, size_t *size)
{
    return call([&] {
        check_interface(handle);
        device_info(find_camera(id), command, {type, output, size});
    });
}

GC_API IFOpenDevice(IF_HANDLE handle, const char *id, DEVICE_ACCESS_FLAGS mode, DEV_HANDLE *device)
{
    return call([&] {
        check_interface(handle);
        require(device, GC_ERR_INVALID_PARAMETER, "missing device output");
        *device = nullptr;
        require(mode == DEVICE_ACCESS_READONLY || mode == DEVICE_ACCESS_CONTROL ||
                mode == DEVICE_ACCESS_EXCLUSIVE, GC_ERR_INVALID_PARAMETER, "unknown device access mode");
        const auto &camera = find_camera(id);
        require(!producer.open.count(camera.channel), GC_ERR_RESOURCE_IN_USE, "serial channel already open");
        auto connection = producer.libraries->connect(camera.channel, control_timeout_ms, &camera);
        require(connection->identity() == camera, GC_ERR_INVALID_ID, "camera identity changed since discovery");
        const auto &config = *producer.libraries->port_config(camera.channel);
        auto xml = std::make_unique<XmlFiles>(*connection, config);
        auto port = make_port(camera, *connection, config, *xml, mode != DEVICE_ACCESS_READONLY);
        auto opened = std::make_unique<Device>(Device{camera, std::move(connection), std::move(xml),
                                                       std::move(port)});
        const auto inserted = producer.open.emplace(camera.channel, std::move(opened));
        *device = inserted.first->second.get();
    });
}

GC_API DevGetInfo(DEV_HANDLE handle, DEVICE_INFO_CMD command, INFO_DATATYPE *type,
                 void *output, size_t *size)
{
    return call([&] { device_info(device_for(handle).identity, command, {type, output, size}); });
}

GC_API DevGetParentIF(DEV_HANDLE handle, IF_HANDLE *interface)
{
    return call([&] {
        device_for(handle);
        require(interface, GC_ERR_INVALID_PARAMETER, "missing parent output");
        *interface = &producer.interface_handle;
    });
}

GC_API DevGetPort(DEV_HANDLE handle, PORT_HANDLE *port)
{
    return call([&] {
        auto &camera = device_for(handle);
        require(port, GC_ERR_INVALID_PARAMETER, "missing remote port output");
        *port = &camera.port;
    });
}

GC_API DevClose(DEV_HANDLE handle)
{
    return call([&] { producer.open.erase(device_for(handle).identity.channel); });
}

GC_API DevGetNumDataStreams(DEV_HANDLE handle, uint32_t *count)
{
    return call([&] {
        device_for(handle);
        require(count, GC_ERR_INVALID_PARAMETER, "missing stream count output");
        *count = 0;
    });
}

GC_API DevGetDataStreamID(DEV_HANDLE handle, uint32_t, char *, size_t *)
{
    return call([&] {
        device_for(handle);
        throw Failure{GC_ERR_NOT_IMPLEMENTED, "CLProtocol serial control has no data streams"};
    });
}

GC_API DevOpenDataStream(DEV_HANDLE handle, const char *, DS_HANDLE *stream)
{
    return call([&] {
        device_for(handle);
        if (stream) *stream = nullptr;
        throw Failure{GC_ERR_NOT_IMPLEMENTED, "CLProtocol serial control has no data streams"};
    });
}

GC_API GCGetPortInfo(PORT_HANDLE handle, PORT_INFO_CMD command, INFO_DATATYPE *type,
                     void *output, size_t *size)
{
    return call([&] {
        const auto &port = device_for(handle, true).port.descriptor();
        const Info out{type, output, size};
        switch (command) {
        case PORT_INFO_ID: return out.text(port.id);
        case PORT_INFO_VENDOR: return out.text(port.vendor);
        case PORT_INFO_MODEL: return out.text(port.model);
        case PORT_INFO_TLTYPE: return out.text(port.tl_type);
        case PORT_INFO_MODULE: return out.text(port.module_name);
        case PORT_INFO_VERSION: return out.text(port.version);
        case PORT_INFO_PORTNAME: return out.text(port.port_name);
        case PORT_INFO_LITTLE_ENDIAN: return out.flag(port.little_endian);
        case PORT_INFO_BIG_ENDIAN: return out.flag(!port.little_endian);
        case PORT_INFO_ACCESS_READ: return out.flag(port.read_access);
        case PORT_INFO_ACCESS_WRITE: return out.flag(port.write_access);
        case PORT_INFO_ACCESS_NA:
        case PORT_INFO_ACCESS_NI: return out.flag(false);
        default: throw Failure{GC_ERR_INVALID_PARAMETER, "unknown port info command"};
        }
    });
}

GC_API GCGetPortURL(PORT_HANDLE handle, char *url, size_t *size)
{
    return call([&] { string(device_for(handle, true).port.urls().front().url, url, size); });
}

GC_API GCGetNumPortURLs(PORT_HANDLE handle, uint32_t *count)
{
    return call([&] {
        const auto &files = device_for(handle, true).port.urls();
        require(count, GC_ERR_INVALID_PARAMETER, "missing URL count output");
        *count = static_cast<uint32_t>(files.size());
    });
}

GC_API GCGetPortURLInfo(PORT_HANDLE handle, uint32_t index, URL_INFO_CMD command,
                       INFO_DATATYPE *type, void *output, size_t *size)
{
    return call([&] {
        const auto &files = device_for(handle, true).port.urls();
        require(index < files.size(), GC_ERR_INVALID_INDEX, "no such URL index");
        const auto &file = files[index];
        const Info out{type, output, size};
        switch (command) {
        case URL_INFO_URL: return out.text(file.url);
        case URL_INFO_SCHEME: return out.number(INFO_DATATYPE_INT32, file.scheme);
        case URL_INFO_FILENAME: return out.text(file.filename);
        case URL_INFO_FILE_SIZE: return out.number(INFO_DATATYPE_UINT64, file.file_size);
        default: break;
        }
        int32_t version;
        switch (command) {
        case URL_INFO_SCHEMA_VER_MAJOR: version = file.schema_major; break;
        case URL_INFO_SCHEMA_VER_MINOR: version = file.schema_minor; break;
        case URL_INFO_FILE_VER_MAJOR: version = file.file_major; break;
        case URL_INFO_FILE_VER_MINOR: version = file.file_minor; break;
        case URL_INFO_FILE_VER_SUBMINOR: version = file.file_subminor; break;
        default: throw Failure{GC_ERR_NOT_AVAILABLE, "XML metadata unavailable"};
        }
        require(version >= 0, GC_ERR_NOT_AVAILABLE, "XML version unavailable");
        return out.number(INFO_DATATYPE_INT32, version);
    });
}

GC_API GCReadPort(PORT_HANDLE handle, uint64_t address, void *buffer, size_t *size)
{
    return call([&] { read_port(device_for(handle, true), address, buffer, size); });
}

GC_API GCWritePort(PORT_HANDLE handle, uint64_t address, const void *buffer, size_t *size)
{
    return call([&] { write_port(device_for(handle, true), address, buffer, size); });
}

GC_API GCReadPortStacked(PORT_HANDLE handle, PORT_REGISTER_STACK_ENTRY *entries, size_t *count)
{
    return call([&] {
        auto &camera = device_for(handle, true);
        require(count && (entries || !*count), GC_ERR_INVALID_PARAMETER, "missing register stack");
        const auto requested = *count;
        for (*count = 0; *count < requested; ++*count) {
            auto &entry = entries[*count];
            read_port(camera, entry.Address, entry.pBuffer, &entry.Size);
        }
    });
}

GC_API GCWritePortStacked(PORT_HANDLE handle, PORT_REGISTER_STACK_ENTRY *entries, size_t *count)
{
    return call([&] {
        auto &camera = device_for(handle, true);
        require(count && (entries || !*count), GC_ERR_INVALID_PARAMETER, "missing register stack");
        const auto requested = *count;
        for (*count = 0; *count < requested; ++*count) {
            auto &entry = entries[*count];
            write_port(camera, entry.Address, entry.pBuffer, &entry.Size);
        }
    });
}
} // namespace GenTL
