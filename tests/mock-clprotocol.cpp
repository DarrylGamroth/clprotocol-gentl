// SPDX-License-Identifier: MIT
#include <CLProtocol/CLProtocol.h>
#include <CLProtocol/ISerial.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace {
constexpr char device_template[] = "FixtureVendor#Family#NoSerial";
constexpr char full_id[] = "/fixture/Linux64_x64#libmock-clprotocol.so#FixtureVendor#Family#NoSerial";
constexpr char first_xml[] = "SchemaVersion.1.1@FixtureVendor#Family#NoSerial@XMLVersion.2.0.0";
constexpr char installed_xml[] = "SchemaVersion.1.1@FixtureVendor#Family#NoSerial@XMLVersion.1.0.0";
constexpr CLINT64 base = 0x20000000100;
constexpr CLUINT32 device_cookie = 7;
bool initialized;
bool connected;
unsigned pending_write;

CLINT32 copy(std::string_view text, CLINT8 *buffer, CLUINT32 *size, bool terminate = true)
{
    if (!size) return CL_ERR_INVALID_PTR;
    const auto needed = static_cast<CLUINT32>(text.size() + terminate);
    if (!buffer || *size < needed) {
        *size = needed;
        return CL_ERR_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, text.data(), text.size());
    if (terminate) buffer[text.size()] = '\0';
    *size = needed;
    return CL_ERR_NO_ERR;
}

CLINT32 exchange(ISerial *port, const char *command, char *reply, CLUINT32 timeout)
{
    std::array<CLINT8, 64> request{};
    CLUINT32 size = static_cast<CLUINT32>(std::strlen(command));
    if (size >= request.size()) return CL_ERR_INVALID_PTR;
    std::memcpy(request.data(), command, size);
    const auto wanted = size;
    auto status = port->clSerialWrite(request.data(), &size, timeout);
    if (status != CL_ERR_NO_ERR) return status;
    if (size != wanted) return CL_ERR_INVALID_REFERENCE;
    size_t offset = 0;
    while (offset < 63) {
        CLUINT32 length = 1;
        status = port->clSerialRead(reply + offset, &length, timeout);
        if (status != CL_ERR_NO_ERR) return status;
        if (length != 1) return CL_ERR_INVALID_REFERENCE;
        if (reply[offset] == '\n') {
            reply[offset] = '\0';
            return CL_ERR_NO_ERR;
        }
        ++offset;
    }
    return CL_ERR_BUFFER_TOO_SMALL;
}

bool valid(CLUINT32 cookie) { return connected && cookie == device_cookie; }
} // namespace

CLINT32 CLPROTOCOL clpInitLib(clp_logger_t, CLP_LOG_LEVEL_VALUE)
{
    if (initialized) return CL_ERR_IN_USE;
    initialized = true;
    return CL_ERR_NO_ERR;
}

CLINT32 CLPROTOCOL clpCloseLib()
{
    initialized = connected = false;
    pending_write = 0;
    return CL_ERR_NO_ERR;
}

CLINT32 CLPROTOCOL clpGetCLProtocolVersion(CLUINT32 *major, CLUINT32 *minor)
{
    *major = 1;
    *minor = 2;
    return CL_ERR_NO_ERR;
}

CLINT32 CLPROTOCOL clpGetShortDeviceIDTemplates(CLINT8 *out, CLUINT32 *size)
{
    if (std::getenv("CLP_MOCK_SINGLE_TEMPLATE")) return copy(device_template, out, size);
    return copy("FixtureVendor#Family#DifferentModel\tFixtureVendor#Family#NoSerial", out, size);
}

CLINT32 CLPROTOCOL clpProbeDevice(ISerial *port, const CLINT8 *selection, CLINT8 *out,
                                  CLUINT32 *size, CLUINT32 *cookie, CLUINT32 timeout)
{
    if (!port || !selection || !cookie || !size) return CL_ERR_INVALID_PTR;
    const std::string_view requested(selection);
    if (requested != device_template && requested != full_id) return CL_ERR_INVALID_DEVICEID;
    auto status = port->clSetBaudRate(CL_BAUDRATE_57600);
    if (status != CL_ERR_NO_ERR) return status;
    char reply[64];
    status = exchange(port, "HELLO?", reply, timeout);
    if (status != CL_ERR_NO_ERR) return status;
    if (std::string_view(reply) != "NoSerial") return CL_ERR_NO_DEVICE_FOUND;
    status = copy(full_id, out, size);
    if (status == CL_ERR_NO_ERR) {
        connected = true;
        *cookie = device_cookie;
    }
    return status;
}

CLINT32 CLPROTOCOL clpDisconnect(CLUINT32 cookie)
{
    if (!valid(cookie)) return CL_ERR_INVALID_COOKIE;
    connected = false;
    pending_write = 0;
    return CL_ERR_NO_ERR;
}

CLINT32 CLPROTOCOL clpGetXMLIDs(ISerial *, CLUINT32 cookie, CLINT8 *out, CLUINT32 *size, CLUINT32)
{
    if (!valid(cookie)) return CL_ERR_INVALID_COOKIE;
    if (std::getenv("CLP_MOCK_XML_FILE_ONLY")) return copy("", out, size);
    char ids[256];
    std::snprintf(ids, sizeof(ids), "%s\t%s", first_xml, installed_xml);
    return copy(ids, out, size);
}

CLINT32 CLPROTOCOL clpGetXMLDescription(ISerial *, CLUINT32 cookie, const CLINT8 *id,
                                      CLINT8 *out, CLUINT32 *size, CLUINT32)
{
    if (!valid(cookie)) return CL_ERR_INVALID_COOKIE;
    if (std::string_view(id) != first_xml) return CL_ERR_NO_XMLDESCRIPTION_FOUND;
    return copy("intentionally invalid XML", out, size, false);
}

CLINT32 CLPROTOCOL clpReadRegister(ISerial *port, CLUINT32 cookie, CLINT64 address, CLINT8 *out,
                                   CLINT64 size, CLUINT32 timeout)
{
    if (!valid(cookie)) return CL_ERR_INVALID_COOKIE;
    if (pending_write) return CL_ERR_IN_USE;
    char reply[64];
    if (address == base && size == 32) {
        auto status = exchange(port, "ID?", reply, timeout);
        if (status != CL_ERR_NO_ERR) return status;
        std::memset(out, 0, size);
        std::memcpy(out, reply, std::strlen(reply) + 1);
        return CL_ERR_NO_ERR;
    }
    if (size != 4) return CL_ERR_INVALID_REFERENCE;
    std::uint32_t bits = 0;
    switch (address - base) {
    case 0x20: {
        auto status = exchange(port, "RATE?", reply, timeout);
        if (status != CL_ERR_NO_ERR) return status;
        const float fps = std::strtof(reply, nullptr);
        std::memcpy(&bits, &fps, sizeof(bits));
        break;
    }
    case 0x24: {
        auto status = exchange(port, "MODE?", reply, timeout);
        if (status != CL_ERR_NO_ERR) return status;
        bits = reply[0] == '1';
        break;
    }
    case 0x28:
        bits = 2;
        break;
    default:
        return CL_ERR_INVALID_REFERENCE;
    }
    for (unsigned i = 0; i < 4; ++i)
        out[i] = static_cast<CLINT8>(bits >> (24 - 8 * i));
    return CL_ERR_NO_ERR;
}

CLINT32 CLPROTOCOL clpWriteRegister(ISerial *port, CLUINT32 cookie, CLINT64 address,
                                    const CLINT8 *data, CLINT64 size, CLUINT32 timeout)
{
    if (!valid(cookie)) return CL_ERR_INVALID_COOKIE;
    if (pending_write) return CL_ERR_IN_USE;
    if (size != 4) return CL_ERR_INVALID_REFERENCE;
    std::uint32_t bits = 0;
    for (unsigned i = 0; i < 4; ++i)
        bits = (bits << 8) | static_cast<unsigned char>(data[i]);
    char command[64], reply[64];
    if (address == base + 0x20) {
        float fps;
        std::memcpy(&fps, &bits, sizeof(fps));
        if (!std::isfinite(fps) || fps <= 0) return CL_ERR_INVALID_REFERENCE;
        std::snprintf(command, sizeof(command), "RATE=%.6f", fps);
    } else if (address == base + 0x24 && bits <= 1) {
        std::snprintf(command, sizeof(command), "MODE=%u", bits);
    } else {
        return CL_ERR_PARAM_READ_ONLY;
    }
    auto status = exchange(port, command, reply, timeout);
    if (status != CL_ERR_NO_ERR) return status;
    if (std::string_view(reply) != "OK") return CL_ERR_INVALID_REFERENCE;
    if (address == base + 0x20) {
        pending_write = 2;
        return CL_ERR_PENDING_WRITE;
    }
    return CL_ERR_NO_ERR;
}

CLINT32 CLPROTOCOL clpContinueWriteRegister(ISerial *, CLUINT32 cookie, BOOL8 wait, CLUINT32)
{
    if (!valid(cookie) || !pending_write) return CL_ERR_INVALID_COOKIE;
    if (!wait) {
        pending_write = 0;
        return CL_ERR_TIMEOUT;
    }
    return --pending_write ? CL_ERR_PENDING_WRITE : CL_ERR_NO_ERR;
}

CLINT32 CLPROTOCOL clpGetErrorText(CLINT32 status, CLINT8 *out, CLUINT32 *size, CLUINT32)
{
    char text[48];
    std::snprintf(text, sizeof(text), "simulated CLProtocol error %d", status);
    return copy(text, out, size);
}
