# CLProtocol GenTL

`clprotocol-gentl.cti` is a standalone, control-only GenTL producer for
Camera Link cameras that have a CLProtocol library. The same CTI can use
different libraries on different configured serial channels. It exposes
remote GenApi XML and register I/O and reports zero data streams. An
application can acquire images through the frame grabber's capture CTI.
The producer statically links the base GenTLProducerCore library for
port I/O, XML URL metadata, information queries, and errors. The CTI
owns the control-only GenTL entry points. GenTLProducerCore is a
build dependency. The installed CTI loads the configured CLSerial
and CLProtocol libraries at runtime.

The implementation targets Linux x86-64 with the GenTL 1.6 and CLProtocol
1.x ABIs. The Camera Link serial library and camera CLProtocol libraries
are installed separately.

## Build

Install the GenTLProducerCore 0.2.5 development package or supply its
source checkout. Point CMake to the EMVA [GenTL and CLProtocol headers](https://www.emva.org/standards-technology/genicam/genicam-downloads/).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/gentl-producer-core/install \
  -DGenTLHeaders_ROOT=/path/to/gentl-1.6 \
  -DCLPROTOCOL_INCLUDE_DIR=/path/to/include
cmake --build build
```

For a source build, use `-DGENTL_PRODUCER_CORE_SOURCE_DIR=/path/to/GenTLProducerCore`
in place of `CMAKE_PREFIX_PATH`. The CLProtocol include directory contains
`CLProtocol/CLProtocol.h`, `CLProtocol/ClSerial.h`, and `CLProtocol/ISerial.h`.
The install target puts the CTI in the configured library directory under
`gentl/`.

## Configure ports

Set `CLPROTOCOL_GENTL_SERIAL` to the Camera Link serial library and
`CLPROTOCOL_GENTL_CONFIG` to a tab-separated file. Each row assigns
one zero-based serial channel to a CLProtocol library and a device
template advertised by that library. The optional last two columns
name the GenApi port and its byte order, which default to `Device`
and `little`. Blank lines and lines beginning with `#` are ignored.

Use an absolute library path or a filename registered in
`GENICAM_CLPROTOCOL`. A filename must resolve uniquely in the registered
roots' `Linux64_x64` directories. The CTI probes only assigned channels
with their assigned libraries.

```sh
export CLPROTOCOL_GENTL_SERIAL=/path/to/libclseregl.so
export CLPROTOCOL_GENTL_CONFIG="$PWD/ports.tsv"
printf '0\t%s\t%s\n' /path/to/libCLProtocol_primary_fixture.so \
  'FixtureVendor#primary_fixture#primary_fixture' > "$CLPROTOCOL_GENTL_CONFIG"

iris-gentl info --cti "$PWD/build/clprotocol-gentl.cti" --json
iris-gentl get --cti "$PWD/build/clprotocol-gentl.cti" \
  --interface CameraLinkSerial --device CLSerial-0 DeviceSerialNumber
```

The GenTL interface is `CameraLinkSerial`. IDs such as `CLSerial-0`
refer to the serial library's channel indexes. Recheck these assignments
if the frame grabber ports change. The camera identity and optional
discovery serial come from its CLProtocol DeviceID. A driver can leave
out the serial token or use a different representation from the hardware
serial. Use exact GenTL IDs to open the port in either case. Iris SPA can
then verify the exact `DeviceSerialNumber` feature before pairing.

XML IDs are retrieved in driver order. The CTI can load XML from the
driver or from matching XML files next to the library or in its
registration root. It publishes local file URLs in a private directory
under `TMPDIR` (or the system temporary directory) until the device closes.
This reserves no camera register addresses for XML.

## Simulator tests

Configure with `-DBUILD_TESTING=ON -DIRIS_GENTL=/path/to/iris-gentl
-Dprimary_fixture_PROVIDER=/path/to/libCLProtocol_primary_fixture.so`, then build and run
`ctest --test-dir build --output-on-failure`. Use an Iris installation
whose GenApi runtime resolves normally. CMake builds the CLSerial
simulator and a second CLProtocol library alongside the producer.

The tests use the same CTI and a generated `build/tests/ports.tsv` for
both camera libraries. The second fixture uses a DeviceID without a
serial token, a different serial protocol and GenApi port name, and
big-endian registers above 4 GiB. Its usable XML is on disk after a
malformed XML candidate. The tests verify discovery, XML loading,
feature read/write, and the absence of data streams. A direct GenTL
test checks that both devices report zero streams and reject stream
enumeration and open requests.

For the Iris SPA integration tests, use `build/clprotocol-gentl.cti`
for `test-control-cti`, `build/tests/libmock-clserial.so` for
`test-clserial`, and `build/tests/ports.tsv` for
`test-clprotocol-gentl-config`. Configure the usual Iris mock
capture CTI and PipeWireAO test daemon as well. Physical Camera Link
control alongside image capture requires hardware qualification.

## GitHub CI

CI pins GenTLProducerCore, the Iris release and its in-tree capture
fixture, the primary_fixture CLProtocol library, and the Iris SPA source
revision. It runs the producer tests with both CLProtocol libraries
and checks the zero-stream ABI, followed by
the Iris SPA suite with scalar and stacked buffer queries. The
paired PipeWireAO daemon test streams and controls both mock cameras.

The GenTLProducerCore, primary_fixture, and Iris SPA checkouts use independent
read-only deploy keys stored as `GENTL_PRODUCER_CORE_READ_KEY`,
`CLPROTOCOL_primary_fixture_READ_KEY`, and `IRIS_SPA_READ_KEY` in the repository's
Actions secrets. The workflow runs on main, same-repository
pull requests, and manual dispatch.
