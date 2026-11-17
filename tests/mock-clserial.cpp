// SPDX-License-Identifier: MIT
#ifdef MOCK_CLALLSERIAL
#include <CLProtocol/ClAllSerial.h>
#define MOCK_SERIAL_CC CLALLSERIALCC
#else
#include <CLProtocol/ClSerial.h>
#define MOCK_SERIAL_CC CLSERIALCC
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>

namespace {
struct Camera {
    bool open = false;
    CLUINT32 baud = CL_BAUDRATE_9600;
    double frame_rate = 120.0;
    bool external_sync = false;
    std::string pending;
};
std::array<Camera, 2> cameras;

std::mutex command_mutex;
std::condition_variable command_changed;
std::string held_command;
bool command_arrived = false;

Camera *camera_for(hSerRef ref)
{
    for (auto &camera : cameras)
        if (ref == &camera && camera.open) return &camera;
    return nullptr;
}
}

extern "C" void clprotocol_mock_hold_command(const char *command)
{
    const std::lock_guard lock(command_mutex);
    held_command = command;
    command_arrived = false;
}

extern "C" int clprotocol_mock_wait_for_command(unsigned timeout_ms)
{
    std::unique_lock lock(command_mutex);
    return command_changed.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                    [] { return command_arrived; });
}

extern "C" void clprotocol_mock_release_command()
{
    const std::lock_guard lock(command_mutex);
    held_command.clear();
    command_changed.notify_all();
}

#ifdef MOCK_CLALLSERIAL
CLINT32 MOCK_SERIAL_CC clGetNumPorts(CLUINT32 *count)
#else
CLINT32 MOCK_SERIAL_CC clGetNumSerialPorts(CLUINT32 *count)
#endif
{
    *count = std::getenv("CLP_MOCK_SINGLE_PORT") ? 1 : cameras.size();
    return CL_ERR_NO_ERR;
}

CLINT32 MOCK_SERIAL_CC clSerialInit(CLUINT32 index, hSerRef *ref)
{
    if (index >= cameras.size()) return CL_ERR_INVALID_INDEX;
    auto &camera = cameras[index];
    if (camera.open) return CL_ERR_PORT_IN_USE;
    camera.open = true;
    *ref = &camera;
    return CL_ERR_NO_ERR;
}

void MOCK_SERIAL_CC clSerialClose(hSerRef ref)
{
    if (auto *camera = camera_for(ref)) camera->open = false;
}

CLINT32 MOCK_SERIAL_CC clGetNumBytesAvail(hSerRef ref, CLUINT32 *count)
{
    auto *camera = camera_for(ref);
    if (!camera) return CL_ERR_INVALID_REFERENCE;
    // Expose a small queued prefix so probe and readback must handle fragmented replies.
    *count = static_cast<CLUINT32>(std::min<std::size_t>(camera->pending.size(), 7));
    return CL_ERR_NO_ERR;
}

CLINT32 MOCK_SERIAL_CC clFlushPort(hSerRef ref)
{
    auto *camera = camera_for(ref);
    if (!camera) return CL_ERR_INVALID_REFERENCE;
    camera->pending.clear();
    return CL_ERR_NO_ERR;
}

CLINT32 MOCK_SERIAL_CC clGetSupportedBaudRates(hSerRef ref, CLUINT32 *rates)
{
    auto *camera = camera_for(ref);
    if (!camera) return CL_ERR_INVALID_REFERENCE;
    *rates = CL_BAUDRATE_9600 | CL_BAUDRATE_57600 | CL_BAUDRATE_115200;
    return CL_ERR_NO_ERR;
}

CLINT32 MOCK_SERIAL_CC clSetBaudRate(hSerRef ref, CLUINT32 baud)
{
    auto *camera = camera_for(ref);
    if (!camera) return CL_ERR_INVALID_REFERENCE;
    if (baud != CL_BAUDRATE_9600 && baud != CL_BAUDRATE_57600 && baud != CL_BAUDRATE_115200) return CL_ERR_BAUD_RATE_NOT_SUPPORTED;
    camera->baud = baud;
    return CL_ERR_NO_ERR;
}

CLINT32 MOCK_SERIAL_CC clSerialRead(hSerRef ref, CLINT8 *buffer, CLUINT32 *size, CLUINT32)
{
    auto *camera = camera_for(ref);
    if (!camera) return CL_ERR_INVALID_REFERENCE;
    if (*size > camera->pending.size()) return CL_ERR_TIMEOUT;
    std::memcpy(buffer, camera->pending.data(), *size);
    camera->pending.erase(0, *size);
    return CL_ERR_NO_ERR;
}

CLINT32 MOCK_SERIAL_CC clSerialWrite(hSerRef ref, CLINT8 *buffer, CLUINT32 *size, CLUINT32 timeout_ms)
try {
    auto *camera = camera_for(ref);
    if (!camera) return CL_ERR_INVALID_REFERENCE;
    const bool other = camera == &cameras[1];
    if (camera->baud != (other ? CL_BAUDRATE_57600 : CL_BAUDRATE_115200))
        return CL_ERR_TIMEOUT;
    std::string command(buffer, *size);
    if (!command.empty() && command.back() == '\n') command.pop_back();
    {
        std::unique_lock lock(command_mutex);
        if (!held_command.empty() && command == held_command) {
            command_arrived = true;
            command_changed.notify_all();
            if (!command_changed.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                          [] { return held_command.empty(); }))
                return CL_ERR_TIMEOUT;
        }
    }
    std::string reply;
    if (command == "HELLO?") reply = other ? "NoSerial" : "WithSerial";
    else if (command == "ID?") reply = other ? "ALT-TEST-001" : "PRIMARY-TEST-001";
    else if (command == "RATE?") reply = std::to_string(camera->frame_rate);
    else if (command.starts_with("RATE=")) {
        camera->frame_rate = std::stod(command.substr(5));
        reply = "OK";
    } else if (command == "MODE?") reply = camera->external_sync ? "1" : "0";
    else if (command == "MODE=0" || command == "MODE=1") {
        camera->external_sync = command == "MODE=1";
        reply = "OK";
    } else return CL_ERR_INVALID_REFERENCE;
    camera->pending = reply + "\n";
    return CL_ERR_NO_ERR;
} catch (const std::exception &) {
    return CL_ERR_INVALID_REFERENCE;
}
