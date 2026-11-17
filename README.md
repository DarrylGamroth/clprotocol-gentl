# CLProtocol GenTL

[![CI](https://github.com/DarrylGamroth/clprotocol-gentl/actions/workflows/ci.yml/badge.svg)](https://github.com/DarrylGamroth/clprotocol-gentl/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/DarrylGamroth/clprotocol-gentl/graph/badge.svg)](https://codecov.io/gh/DarrylGamroth/clprotocol-gentl)

`clprotocol-gentl.cti` is a standalone GenTL producer for Camera Link camera
control on Linux x86-64. It exposes remote GenApi XML and register I/O and
reports zero data streams. Applications can acquire images through the frame
grabber's capture CTI.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /path/to/install
```

CMake downloads the EMVA CLProtocol SDK headers and uses the GenTL 1.6 headers
provided by GenTLProducerCore. It uses an installed GenTLProducerCore package
when available and otherwise fetches a pinned revision from GitHub. To use a local checkout,
set `GENTL_PRODUCER_CORE_SOURCE_DIR`. To supply local headers, set
`CLPROTOCOL_INCLUDE_DIR` and `GenTLHeaders_ROOT`.

The CTI statically links GenTLProducerCore. The install target places it under
`lib/gentl` by default.

## Discover cameras

Install your camera's CLProtocol driver under `Linux64_x64` in a registration
directory. Place any accompanying XML in the registration directory. Make the
EMVA CLAllSerial runtime available to the dynamic loader or place it under the
same `Linux64_x64` directory with the frame grabber's Camera Link serial
module alongside it.

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

GitHub CI uses in-tree mocks to exercise environment-based discovery, XML
loading, register I/O, and the zero-stream ABI, and uploads GCC coverage to
Codecov. The paired Iris SPA tests stream mock frames while accessing camera features. See the
[workflow](.github/workflows/ci.yml) for the test dependencies and commands.
