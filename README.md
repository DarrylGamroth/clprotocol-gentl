# CLProtocol GenTL

[![CI](https://github.com/DarrylGamroth/clprotocol-gentl/actions/workflows/ci.yml/badge.svg)](https://github.com/DarrylGamroth/clprotocol-gentl/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/DarrylGamroth/clprotocol-gentl/graph/badge.svg)](https://codecov.io/gh/DarrylGamroth/clprotocol-gentl)

`clprotocol-gentl.cti` exposes Camera Link camera control through GenTL.
Applications use it to load the camera's GenApi XML and read or write
registers. The producer reports zero data streams. For image acquisition, use
the frame grabber's capture CTI.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /path/to/install
```

CMake downloads the EMVA CLProtocol SDK headers for the Linux target and uses
GenTL 1.6 headers through GenTLProducerCore. It uses an installed Core package
or fetches a pinned revision. To use a local Core checkout, set
`GENTL_PRODUCER_CORE_SOURCE_DIR`. To use local headers, set
`CLPROTOCOL_INCLUDE_DIR` and `GenTLHeaders_ROOT`.

The install target places the CTI under `lib/gentl` by default.

## Platforms

The producer uses the CLProtocol SDK and the matching CLAllSerial runtime and
camera driver. GenApi runs in the consumer. The producer's Linux loader and
GenTLProducerCore determine which systems can build and load the CTI.

GitHub CI runs the mock suite on Linux x86-64 and AArch64. It cross-builds
and runs the suite under emulation for 32-bit ARM hard-float. Windows
requires a port of the CTI loader and Core. EMVA ships GenApi for macOS but
its CLProtocol package has no macOS runtime.

## Discover cameras

Install the camera driver in your platform subdirectory of a CLProtocol
registration directory.

| Linux target | Platform subdirectory |
| --- | --- |
| x86-64 | `Linux64_x64` |
| AArch64 | `Linux64_ARM` |
| ARM hard-float (32-bit) | `Linux32_ARMhf` |

Place any accompanying XML in the registration directory. Make the EMVA
CLAllSerial runtime available to the dynamic loader or place it in the same
platform subdirectory. Install the frame grabber's Camera Link serial module
alongside CLAllSerial.

```sh
export GENICAM_CLPROTOCOL=/path/to/CLProtocol
iris-gentl info --cti "$PWD/build/clprotocol-gentl.cti" --json
iris-gentl get --cti "$PWD/build/clprotocol-gentl.cti" \
  --interface CameraLinkSerial --device CLSerial-0 DeviceSerialNumber
```

The producer queries the registered drivers for their CLProtocol device
templates and uses CLAllSerial to enumerate serial ports. With one registered
template, the producer probes the enumerated ports with that template. When
several templates are registered, select the camera's advertised template
with `CLPROTOCOL_GENTL_DEVICE_TEMPLATE` before discovery. Use
`CLPROTOCOL_GENTL_SERIAL` to override CLAllSerial with a specific serial library
when necessary. See the [EMVA CLProtocol standard](https://www.emva.org/wp-content/uploads/GenICam_CLProtocol_Standard_v1.2.pdf#page=4).

Existing installations can still use `CLPROTOCOL_GENTL_CONFIG` to assign
different drivers to individual ports. The tab-separated columns are the
zero-based port index, the library path or registered filename, the short
DeviceID template, the optional GenApi port name, and the optional byte
order. The defaults are `Device` and `little`.

The GenTL interface ID is `CameraLinkSerial`. Device IDs such as
`CLSerial-0` refer to the serial library's port indexes. When the CLProtocol
DeviceID omits the serial number, use the GenTL interface and device IDs to
open the camera. Applications can verify `DeviceSerialNumber` through GenApi
when the camera exposes it.

The producer can load XML from the driver or from a matching installed XML
file. It publishes local file URLs in a private temporary directory until
the device closes.

## Simulator tests

The in-tree mocks test environment-based discovery, XML loading, register I/O,
and the zero-stream ABI. Configure with `-DBUILD_TESTING=ON` and set
`-DCLPROTOCOL_GENTL_TEST_IRIS=OFF` to run the direct GenTL tests without
`iris-gentl`. The x86-64 workflow requires above 90% line coverage and uploads
the report to Codecov. The paired Iris SPA tests stream mock frames while
accessing camera features. See the [workflow](.github/workflows/ci.yml).
