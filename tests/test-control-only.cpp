// SPDX-License-Identifier: MIT
#include <GenTL.h>
#include <dlfcn.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace GenTL;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        std::exit(EXIT_FAILURE); \
    } \
} while (false)

int main(int argc, char **argv)
{
    CHECK(argc >= 3);
    auto *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) std::fprintf(stderr, "%s\n", dlerror());
    CHECK(library);
#define LOAD(name) const auto name = reinterpret_cast<P##name>(dlsym(library, #name)); CHECK(name)
    LOAD(GCInitLib);
    LOAD(GCCloseLib);
    LOAD(GCGetLastError);
    LOAD(TLOpen);
    LOAD(TLClose);
    LOAD(TLOpenInterface);
    LOAD(IFClose);
    LOAD(IFUpdateDeviceList);
    LOAD(IFOpenDevice);
    LOAD(DevClose);
    LOAD(DevGetNumDataStreams);
    LOAD(DevGetDataStreamID);
    LOAD(DevOpenDataStream);
    LOAD(DevGetPort);
    LOAD(GCReadPort);
    LOAD(GCWritePort);
#undef LOAD
    CHECK(GCInitLib() == GC_ERR_SUCCESS);
    TL_HANDLE system = nullptr;
    IF_HANDLE interface = nullptr;
    CHECK(TLOpen(&system) == GC_ERR_SUCCESS);
    CHECK(TLOpenInterface(system, "CameraLinkSerial", &interface) == GC_ERR_SUCCESS);
    CHECK(IFUpdateDeviceList(interface, nullptr, 1000) == GC_ERR_SUCCESS);

    for (int i = 2; i < argc; ++i) {
        const char *id = argv[i];
        DEV_HANDLE device = nullptr;
        CHECK(IFOpenDevice(interface, id, DEVICE_ACCESS_READONLY, &device) == GC_ERR_SUCCESS);
        uint32_t count = 99;
        CHECK(DevGetNumDataStreams(device, &count) == GC_ERR_SUCCESS && count == 0);
        size_t size = 0;
        CHECK(DevGetDataStreamID(device, 0, nullptr, &size) == GC_ERR_NOT_IMPLEMENTED);
        DS_HANDLE stream = device;
        CHECK(DevOpenDataStream(device, "stream0", &stream) == GC_ERR_NOT_IMPLEMENTED && !stream);
        GC_ERROR last = GC_ERR_SUCCESS;
        CHECK(GCGetLastError(&last, nullptr, &size) == GC_ERR_SUCCESS);
        CHECK(last == GC_ERR_NOT_IMPLEMENTED && size > 1);

        PORT_HANDLE port = nullptr;
        CHECK(DevGetPort(device, &port) == GC_ERR_SUCCESS);
        constexpr uint64_t alt_address = 0x20000000100;
        std::array<char, 32> serial{};
        if (std::strcmp(id, "CLSerial-1") == 0) {
            size = serial.size();
            CHECK(GCReadPort(port, alt_address, serial.data(), &size) == GC_ERR_SUCCESS);
            CHECK(size == serial.size() && std::strcmp(serial.data(), "ALT-TEST-001") == 0);
        }
        size = serial.size();
        CHECK(GCWritePort(port, alt_address, serial.data(), &size) == GC_ERR_ACCESS_DENIED && size == 0);
        size = serial.size();
        CHECK(GCReadPort(port, std::numeric_limits<int64_t>::max(), serial.data(), &size)
                  == GC_ERR_INVALID_ADDRESS && size == 0);
        CHECK(DevClose(device) == GC_ERR_SUCCESS);
    }
    CHECK(IFClose(interface) == GC_ERR_SUCCESS);
    CHECK(TLClose(system) == GC_ERR_SUCCESS);
    CHECK(GCCloseLib() == GC_ERR_SUCCESS);
    CHECK(dlclose(library) == 0);
    std::puts("Devices reported zero streams and rejected stream enumeration and open");
}
