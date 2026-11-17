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
    LOAD(GCGetInfo);
    LOAD(TLGetInfo);
    LOAD(TLGetInterfaceInfo);
    LOAD(TLUpdateInterfaceList);
    LOAD(TLGetInterfaceID);
    LOAD(TLOpenInterface);
    LOAD(IFClose);
    LOAD(IFGetParentTL);
    LOAD(IFGetInfo);
    LOAD(IFUpdateDeviceList);
    LOAD(IFGetNumDevices);
    LOAD(IFGetDeviceID);
    LOAD(IFGetDeviceInfo);
    LOAD(IFOpenDevice);
    LOAD(DevClose);
    LOAD(DevGetParentIF);
    LOAD(DevGetInfo);
    LOAD(DevGetNumDataStreams);
    LOAD(DevGetDataStreamID);
    LOAD(DevOpenDataStream);
    LOAD(DevGetPort);
    LOAD(GCGetPortInfo);
    LOAD(GCGetPortURL);
    LOAD(GCGetPortURLInfo);
    LOAD(GCGetNumPortURLs);
    LOAD(GCReadPort);
    LOAD(GCWritePort);
    LOAD(GCReadPortStacked);
    LOAD(GCWritePortStacked);
#undef LOAD
    CHECK(TLOpen(nullptr) == GC_ERR_NOT_INITIALIZED);
    CHECK(GCInitLib() == GC_ERR_SUCCESS);
    CHECK(GCInitLib() == GC_ERR_RESOURCE_IN_USE);

#define UNSUPPORTED(name, ...) \
    do { \
        const auto test = reinterpret_cast<P##name>(dlsym(library, #name)); \
        CHECK(test && test(__VA_ARGS__) == GC_ERR_NOT_IMPLEMENTED); \
    } while (false)
    UNSUPPORTED(GCRegisterEvent, nullptr, EVENT_NEW_BUFFER, nullptr);
    UNSUPPORTED(GCUnregisterEvent, nullptr, EVENT_NEW_BUFFER);
    UNSUPPORTED(EventGetData, nullptr, nullptr, nullptr, 0);
    UNSUPPORTED(EventGetDataInfo, nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr);
    UNSUPPORTED(EventGetInfo, nullptr, 0, nullptr, nullptr, nullptr);
    UNSUPPORTED(EventFlush, nullptr);
    UNSUPPORTED(EventKill, nullptr);
    UNSUPPORTED(DSAnnounceBuffer, nullptr, nullptr, 0, nullptr, nullptr);
    UNSUPPORTED(DSAllocAndAnnounceBuffer, nullptr, 0, nullptr, nullptr);
    UNSUPPORTED(DSFlushQueue, nullptr, ACQ_QUEUE_ALL_DISCARD);
    UNSUPPORTED(DSStartAcquisition, nullptr, ACQ_START_FLAGS_DEFAULT, 1);
    UNSUPPORTED(DSStopAcquisition, nullptr, ACQ_STOP_FLAGS_DEFAULT);
    UNSUPPORTED(DSGetInfo, nullptr, STREAM_INFO_ID, nullptr, nullptr, nullptr);
    UNSUPPORTED(DSGetBufferID, nullptr, 0, nullptr);
    UNSUPPORTED(DSClose, nullptr);
    UNSUPPORTED(DSRevokeBuffer, nullptr, nullptr, nullptr, nullptr);
    UNSUPPORTED(DSQueueBuffer, nullptr, nullptr);
    UNSUPPORTED(DSGetBufferInfo, nullptr, nullptr, BUFFER_INFO_BASE, nullptr, nullptr, nullptr);
    UNSUPPORTED(DSGetBufferChunkData, nullptr, nullptr, nullptr, nullptr);
    UNSUPPORTED(DSGetParentDev, nullptr, nullptr);
    UNSUPPORTED(DSGetNumBufferParts, nullptr, nullptr, nullptr);
    UNSUPPORTED(DSGetBufferPartInfo, nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr);
    UNSUPPORTED(DSAnnounceCompositeBuffer, nullptr, 0, nullptr, nullptr, nullptr, nullptr);
    UNSUPPORTED(DSGetBufferInfoStacked, nullptr, nullptr, nullptr, 0);
    UNSUPPORTED(DSGetBufferPartInfoStacked, nullptr, nullptr, nullptr, 0);
    UNSUPPORTED(DSGetNumFlows, nullptr, nullptr);
    UNSUPPORTED(DSGetFlowInfo, nullptr, 0, 0, nullptr, nullptr, nullptr);
    UNSUPPORTED(DSGetNumBufferSegments, nullptr, nullptr, nullptr);
    UNSUPPORTED(DSGetBufferSegmentInfo, nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr);
#undef UNSUPPORTED

    const auto info = [](auto query, auto command, auto... args) {
        std::array<char, 4096> data{};
        INFO_DATATYPE type = INFO_DATATYPE_UNKNOWN;
        size_t size = data.size();
        CHECK(query(args..., command, &type, data.data(), &size) == GC_ERR_SUCCESS);
        CHECK(size && size <= data.size() && type != INFO_DATATYPE_UNKNOWN);
    };
    TL_HANDLE system = nullptr;
    IF_HANDLE interface = nullptr;
    CHECK(TLOpen(&system) == GC_ERR_SUCCESS);
    constexpr const char *interface_id = "CameraLinkSerial";
    for (auto command : {TL_INFO_ID, TL_INFO_VENDOR, TL_INFO_MODEL, TL_INFO_DISPLAYNAME,
                         TL_INFO_VERSION, TL_INFO_TLTYPE, TL_INFO_NAME, TL_INFO_PATHNAME,
                         TL_INFO_CHAR_ENCODING, TL_INFO_GENTL_VER_MAJOR, TL_INFO_GENTL_VER_MINOR}) {
        info(GCGetInfo, command);
        info(TLGetInfo, command, system);
    }
    CHECK(TLGetInfo(system, -1, nullptr, nullptr, nullptr) == GC_ERR_INVALID_PARAMETER);
    bool8_t changed = 1;
    CHECK(TLUpdateInterfaceList(system, &changed, 0) == GC_ERR_SUCCESS && !changed);
    CHECK(TLGetInterfaceID(system, 1, nullptr, nullptr) == GC_ERR_INVALID_INDEX);
    CHECK(TLGetInterfaceInfo(system, "missing", INTERFACE_INFO_ID, nullptr, nullptr, nullptr)
          == GC_ERR_INVALID_ID);
    CHECK(TLOpenInterface(system, "CameraLinkSerial", &interface) == GC_ERR_SUCCESS);
    TL_HANDLE parent = nullptr;
    CHECK(IFGetParentTL(interface, &parent) == GC_ERR_SUCCESS && parent == system);
    for (auto command : {INTERFACE_INFO_ID, INTERFACE_INFO_DISPLAYNAME, INTERFACE_INFO_TLTYPE}) {
        info(TLGetInterfaceInfo, command, system, interface_id);
        info(IFGetInfo, command, interface);
    }
    CHECK(IFGetInfo(interface, -1, nullptr, nullptr, nullptr) == GC_ERR_INVALID_PARAMETER);
    CHECK(IFUpdateDeviceList(interface, &changed, 1000) == GC_ERR_SUCCESS && changed);
    CHECK(IFUpdateDeviceList(interface, &changed, 0) == GC_ERR_SUCCESS && !changed);
    uint32_t devices = 0;
    CHECK(IFGetNumDevices(interface, &devices) == GC_ERR_SUCCESS);
    CHECK(devices == static_cast<uint32_t>(argc - 2));
    CHECK(IFGetDeviceID(interface, devices, nullptr, nullptr) == GC_ERR_INVALID_INDEX);

    for (int i = 2; i < argc; ++i) {
        const char *id = argv[i];
        DEV_HANDLE device = nullptr;
        CHECK(IFOpenDevice(interface, id, 0, &device) == GC_ERR_INVALID_PARAMETER);
        CHECK(IFOpenDevice(interface, id, DEVICE_ACCESS_READONLY, &device) == GC_ERR_SUCCESS);
        DEV_HANDLE duplicate = nullptr;
        CHECK(IFOpenDevice(interface, id, DEVICE_ACCESS_EXCLUSIVE, &duplicate) == GC_ERR_RESOURCE_IN_USE);
        IF_HANDLE parent_interface = nullptr;
        CHECK(DevGetParentIF(device, &parent_interface) == GC_ERR_SUCCESS && parent_interface == interface);
        for (auto command : {DEVICE_INFO_ID, DEVICE_INFO_VENDOR, DEVICE_INFO_MODEL,
                             DEVICE_INFO_TLTYPE, DEVICE_INFO_DISPLAYNAME, DEVICE_INFO_ACCESS_STATUS}) {
            info(IFGetDeviceInfo, command, interface, id);
            info(DevGetInfo, command, device);
        }
        CHECK(DevGetInfo(device, -1, nullptr, nullptr, nullptr) == GC_ERR_NOT_AVAILABLE);
        CHECK(IFUpdateDeviceList(interface, &changed, 0) == GC_ERR_SUCCESS && !changed);
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
        for (auto command : {PORT_INFO_ID, PORT_INFO_VENDOR, PORT_INFO_MODEL, PORT_INFO_TLTYPE,
                             PORT_INFO_MODULE, PORT_INFO_VERSION, PORT_INFO_PORTNAME,
                             PORT_INFO_LITTLE_ENDIAN, PORT_INFO_BIG_ENDIAN, PORT_INFO_ACCESS_READ,
                             PORT_INFO_ACCESS_WRITE, PORT_INFO_ACCESS_NA, PORT_INFO_ACCESS_NI}) {
            info(GCGetPortInfo, command, port);
        }
        CHECK(GCGetPortInfo(port, -1, nullptr, nullptr, nullptr) == GC_ERR_INVALID_PARAMETER);
        CHECK(GCGetPortURL(port, nullptr, &size) == GC_ERR_SUCCESS && size > 1);
        CHECK(GCGetNumPortURLs(port, &count) == GC_ERR_SUCCESS && count > 0);
        for (auto command : {URL_INFO_URL, URL_INFO_SCHEME, URL_INFO_FILENAME, URL_INFO_FILE_SIZE,
                             URL_INFO_SCHEMA_VER_MAJOR, URL_INFO_SCHEMA_VER_MINOR, URL_INFO_FILE_VER_MAJOR,
                             URL_INFO_FILE_VER_MINOR, URL_INFO_FILE_VER_SUBMINOR}) {
            info(GCGetPortURLInfo, command, port, uint32_t{0});
        }
        CHECK(GCGetPortURLInfo(port, count, URL_INFO_URL, nullptr, nullptr, nullptr) == GC_ERR_INVALID_INDEX);
        CHECK(GCGetPortURLInfo(port, 0, -1, nullptr, nullptr, nullptr) == GC_ERR_NOT_AVAILABLE);
        constexpr uint64_t alt_address = 0x20000000100;
        std::array<char, 32> serial{};
        if (std::strcmp(id, "CLSerial-1") == 0) {
            size = serial.size();
            CHECK(GCReadPort(port, alt_address, serial.data(), &size) == GC_ERR_SUCCESS);
            CHECK(size == serial.size() && std::strcmp(serial.data(), "ALT-TEST-001") == 0);
            uint32_t mode = 0;
            PORT_REGISTER_STACK_ENTRY entries[] = {
                {alt_address, serial.data(), serial.size()}, {alt_address + 0x24, &mode, sizeof(mode)}};
            size_t entry_count = 2;
            CHECK(GCReadPortStacked(port, entries, &entry_count) == GC_ERR_SUCCESS && entry_count == 2);
            CHECK(std::strcmp(serial.data(), "ALT-TEST-001") == 0 && mode == 0);
            CHECK(GCWritePortStacked(port, entries, &entry_count) == GC_ERR_ACCESS_DENIED && entry_count == 0);
            CHECK(GCReadPort(port, alt_address, nullptr, &size) == GC_ERR_INVALID_PARAMETER);
        }
        size = serial.size();
        CHECK(GCWritePort(port, alt_address, serial.data(), &size) == GC_ERR_ACCESS_DENIED && size == 0);
        size = serial.size();
        CHECK(GCReadPort(port, std::numeric_limits<int64_t>::max(), serial.data(), &size)
                  == GC_ERR_INVALID_ADDRESS && size == 0);
        CHECK(DevClose(device) == GC_ERR_SUCCESS);
        CHECK(DevClose(device) == GC_ERR_INVALID_HANDLE);

        if (std::strcmp(id, "CLSerial-1") == 0) {
            CHECK(IFOpenDevice(interface, id, DEVICE_ACCESS_CONTROL, &device) == GC_ERR_SUCCESS);
            CHECK(DevGetPort(device, &port) == GC_ERR_SUCCESS);
            // The fixture exposes TriggerMode as a big-endian 32-bit register.
            std::array<unsigned char, 4> trigger_mode{0, 0, 0, 1};
            size = trigger_mode.size();
            CHECK(GCWritePort(port, alt_address + 0x24, trigger_mode.data(), &size) == GC_ERR_SUCCESS);
            decltype(trigger_mode) readback{};
            size = readback.size();
            CHECK(GCReadPort(port, alt_address + 0x24, readback.data(), &size) == GC_ERR_SUCCESS);
            CHECK(size == readback.size() && readback == trigger_mode);
            CHECK(DevClose(device) == GC_ERR_SUCCESS);
        }
    }
    CHECK(IFClose(interface) == GC_ERR_SUCCESS);
    CHECK(TLClose(system) == GC_ERR_SUCCESS);
    CHECK(GCCloseLib() == GC_ERR_SUCCESS);
    CHECK(dlclose(library) == 0);
    std::puts("Devices reported zero streams and rejected stream enumeration and open");
}
