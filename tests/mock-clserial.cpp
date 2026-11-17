// SPDX-License-Identifier: MIT
#include <CLProtocol/ClSerial.h>

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

extern "C" void primary_fixture_sim_hold_command(const char *command)
{
    const std::lock_guard lock(command_mutex);
    held_command = command;
    command_arrived = false;
}

extern "C" int primary_fixture_sim_wait_for_command(unsigned timeout_ms)
{
    std::unique_lock lock(command_mutex);
    return command_changed.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                    [] { return command_arrived; });
}

extern "C" void primary_fixture_sim_release_command()
{
    const std::lock_guard lock(command_mutex);
    held_command.clear();
    command_changed.notify_all();
}

CLINT32 CLSERIALCC clGetNumSerialPorts(CLUINT32 *count)
{
    *count = cameras.size();
    return CL_ERR_NO_ERR;
}

CLINT32 CLSERIALCC clSerialInit(CLUINT32 index, hSerRef *ref)
{
    if (index >= cameras.size()) return CL_ERR_INVALID_INDEX;
    auto &camera = cameras[index];
    if (camera.open) return CL_ERR_PORT_IN_USE;
    camera.open = true;
    *ref = &camera;
    return CL_ERR_NO_ERR;
}

void CLSERIALCC clSerialClose(hSerRef ref)
{
    if (auto *camera = camera_for(ref)) camera->open = false;
}

CLINT32 CLSERIALCC clGetNumBytesAvail(hSerRef ref, CLUINT32 *count)
{
    auto *camera = camera_for(ref);
    if (!camera) return CL_ERR_INVALID_REFERENCE;
    // Expose a small queued prefix so probe and readback must handle fragmented replies.
    *count = static_cast<CLUINT32>(std::min<std::size_t>(camera->pending.size(), 7));
    return CL_ERR_NO_ERR;
}

CLINT32 CLSERIALCC clFlushPort(hSerRef ref)
{
    auto *camera = camera_for(ref);
    if (!camera) return CL_ERR_INVALID_REFERENCE;
    camera->pending.clear();
    return CL_ERR_NO_ERR;
}

CLINT32 CLSERIALCC clGetSupportedBaudRates(hSerRef ref, CLUINT32 *rates)
{
    auto *camera = camera_for(ref);
    if (!camera) return CL_ERR_INVALID_REFERENCE;
    *rates = CL_BAUDRATE_9600 | CL_BAUDRATE_57600 | CL_BAUDRATE_115200;
    return CL_ERR_NO_ERR;
}

CLINT32 CLSERIALCC clSetBaudRate(hSerRef ref, CLUINT32 baud)
{
    auto *camera = camera_for(ref);
    if (!camera) return CL_ERR_INVALID_REFERENCE;
    if (baud != CL_BAUDRATE_9600 && baud != CL_BAUDRATE_57600 && baud != CL_BAUDRATE_115200) return CL_ERR_BAUD_RATE_NOT_SUPPORTED;
    camera->baud = baud;
    return CL_ERR_NO_ERR;
}

CLINT32 CLSERIALCC clSerialRead(hSerRef ref, CLINT8 *buffer, CLUINT32 *size, CLUINT32)
{
    auto *camera = camera_for(ref);
    if (!camera) return CL_ERR_INVALID_REFERENCE;
    if (*size > camera->pending.size()) return CL_ERR_TIMEOUT;
    std::memcpy(buffer, camera->pending.data(), *size);
    camera->pending.erase(0, *size);
    return CL_ERR_NO_ERR;
}

CLINT32 CLSERIALCC clSerialWrite(hSerRef ref, CLINT8 *buffer, CLUINT32 *size, CLUINT32 timeout_ms)
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
    if (other) {
        if (command == "HELLO?") reply = "NoSerial";
        else if (command == "ID?") reply = "ALT-TEST-001";
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
    }
    if (command == "cameratype raw")
        reply = std::getenv("primary_fixture_SIM_NO_CAMERA") ? "different model" : "primary_fixture";
    else if (command == "hwuid raw") {
        const char *serial = std::getenv("primary_fixture_SIM_HARDWARE_ID");
        reply = serial ? serial : "primary_fixture-TEST-001";
    }
    else if (command == "fps raw") reply = std::to_string(camera->frame_rate);
    else if (command == "minfps raw") reply = "0.001";
    else if (command == "maxfps raw") reply = "600";
    else if (command.starts_with("set fps ")) {
        camera->frame_rate = std::stod(command.substr(8));
        reply = "Result: OK";
    } else if (command == "cropping raw") {
        reply = std::getenv("primary_fixture_SIM_CROP_COLUMNS") ? "on" : "off";
    } else if (command == "cropping columns raw") {
        const char *columns = std::getenv("primary_fixture_SIM_CROP_COLUMNS");
        reply = columns ? columns : "0 639";
    } else if (command == "cropping rows raw") {
        reply = "0 3";
    } else if (command == "extsynchro raw") {
        reply = camera->external_sync ? "on" : "off";
    } else if (command == "set extsynchro on" || command == "set extsynchro off") {
        camera->external_sync = command == "set extsynchro on";
        reply = "Result: OK";
    } else reply = "Error: unknown command";
    camera->pending = reply + "\r\nfli-cli>";
    return CL_ERR_NO_ERR;
} catch (const std::exception &) {
    return CL_ERR_INVALID_REFERENCE;
}
