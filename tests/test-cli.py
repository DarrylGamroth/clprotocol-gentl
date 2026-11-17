#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
import xml.etree.ElementTree as ET


def run(env, command, *args, expect=0):
    argv = [iris, command, "--cti", cti, *args]
    result = subprocess.run(argv, capture_output=True, text=True, env=env, timeout=8)
    if result.returncode != expect:
        raise AssertionError(f"{argv}\nexit {result.returncode}\n{result.stdout}{result.stderr}")
    return result.stdout


def json_lines(text):
    return [json.loads(line) for line in text.splitlines()]


def verify_camera(env, channel, serial, port):
    selector = ["--interface", "CameraLinkSerial", "--device", f"CLSerial-{channel}"]
    xml = ET.fromstring(run(env, "xml", *selector))
    nodes = {node.attrib.get("Name") for node in xml}
    assert {port, "DeviceSerialNumber", "AcquisitionFrameRate"} <= nodes, nodes
    features = ("DeviceSerialNumber", "AcquisitionFrameRate", "TriggerMode")
    before = run(env, "get", *selector, "--json", *features)
    values = {entry["name"]: entry["value"] for entry in json_lines(before)}
    assert values["DeviceSerialNumber"] == serial, values
    assert (float(values["AcquisitionFrameRate"]), values["TriggerMode"]) == (120, "Off"), values
    after = run(env, "get", *selector, "--json", "--set", "AcquisitionFrameRate=200",
                "--set", "TriggerMode=On", *features)
    values = {entry["name"]: entry["value"] for entry in json_lines(after)}
    assert values["DeviceSerialNumber"] == serial, values
    assert (float(values["AcquisitionFrameRate"]), values["TriggerMode"]) == (200, "On"), values
    run(env, "acquire", *selector, "--stream-only", "1", expect=1)
    print(f"{serial}: loaded XML via {port}, verified writes and readback, zero streams")


iris, cti, mock_serial, mock_all_serial, mock_protocol, config, platform_directory = sys.argv[1:]
env = os.environ.copy()
for name in ("CLP_MOCK_XML_FILE_ONLY", "CLP_MOCK_SINGLE_TEMPLATE", "CLP_MOCK_SINGLE_PORT",
             "CLPROTOCOL_GENTL_CONFIG", "CLPROTOCOL_GENTL_SERIAL", "CLPROTOCOL_GENTL_DEVICE_TEMPLATE",
             "GENICAM_CLPROTOCOL"):
    env.pop(name, None)
with TemporaryDirectory(prefix="clprotocol-gentl XML ") as tmp:
    env["TMPDIR"] = tmp
    paired = env | {"CLPROTOCOL_GENTL_CONFIG": config, "CLPROTOCOL_GENTL_SERIAL": mock_serial}
    found = json_lines(run(paired, "info", "--json"))
    assert len(found) == 2 and {e["cti"] for e in found} == {cti}, found
    assert found[0]["model"] == "WithSerial" and found[0]["serial"] == "PRIMARY-TEST-001", found
    assert found[1]["model"] == "NoSerial" and not found[1].get("serial"), found
    verify_camera(paired, 0, "PRIMARY-TEST-001", "CameraPort")
    verify_camera(paired, 1, "ALT-TEST-001", "CameraPort")
    assert not list(Path(tmp).iterdir()), "XML files leaked after device close"

    driver = Path(mock_protocol)
    registration = Path(tmp) / "CLProtocol"
    platform = registration / platform_directory
    platform.mkdir(parents=True)
    # Keep the driver target separate from its registration root and XML.
    (platform / driver.name).symlink_to(driver)
    (platform / "libCLAllSerial_gcc8_v3_5.so").symlink_to(Path(mock_all_serial).resolve())
    for xml_file in driver.parent.parent.glob("*.xml"):
        shutil.copyfile(xml_file, registration / xml_file.name)
    registered = Path(tmp) / "registered.tsv"
    registered.write_text(f"1\t{driver.name}\tFixtureVendor#Family#NoSerial\tCameraPort\tbig\n")
    explicit = env | {"GENICAM_CLPROTOCOL": str(registration),
                      "CLPROTOCOL_GENTL_CONFIG": str(registered), "CLP_MOCK_XML_FILE_ONLY": "1"}
    found = json_lines(run(explicit, "info", "--json"))
    assert len(found) == 1 and found[0]["device"] == "CLSerial-1", found
    verify_camera(explicit, 1, "ALT-TEST-001", "CameraPort")

    saved_config = registered.read_text()
    for invalid_config in ("# no assignments\n",
                           saved_config.replace(driver.name, "libunknown.so"),
                           saved_config.replace(driver.name, " relative/" + driver.name),
                           saved_config.replace("\tbig\n", "\tinvalid\n")):
        registered.write_text(invalid_config)
        run(explicit, "info", "--json", expect=1)
    registered.unlink()
    run(explicit, "info", "--json", expect=1)

    automatic = env | {"GENICAM_CLPROTOCOL": str(registration)}
    run(automatic, "info", "--json", expect=1)  # Two advertised templates.
    automatic["CLPROTOCOL_GENTL_DEVICE_TEMPLATE"] = "FixtureVendor#Family#Unknown"
    run(automatic, "info", "--json", expect=1)
    automatic["CLPROTOCOL_GENTL_DEVICE_TEMPLATE"] = "FixtureVendor#Family#NoSerial"
    assert json_lines(run(automatic, "info", "--json")) == found
    automatic.pop("CLPROTOCOL_GENTL_DEVICE_TEMPLATE")
    automatic["CLP_MOCK_SINGLE_TEMPLATE"] = "1"
    for xml_file in registration.glob("*.xml"):
        xml_file.write_text(xml_file.read_text().replace("CameraPort", "Device"))
    assert json_lines(run(automatic, "info", "--json")) == found
    verify_camera(automatic, 1, "ALT-TEST-001", "Device")

    # A reachable camera without XML can be discovered, but opening fails and
    # the producer removes the private directory it reserved for the XML.
    no_xml = Path(tmp) / "no-xml"
    no_xml_platform = no_xml / platform_directory
    no_xml_platform.mkdir(parents=True)
    shutil.copyfile(driver, no_xml_platform / driver.name)
    isolated = automatic | {"GENICAM_CLPROTOCOL": str(no_xml),
                            "CLPROTOCOL_GENTL_SERIAL": mock_serial, "CLP_MOCK_XML_FILE_ONLY": "1"}
    assert json_lines(run(isolated, "info", "--json")) == found
    run(isolated, "xml", "--interface", "CameraLinkSerial", "--device", "CLSerial-1", expect=1)
    assert not any(entry.name.startswith("clprotocol-gentl-") for entry in Path(tmp).iterdir())

    (platform / "libCLAllSerial_gcc8_v3_5.so").unlink()
    automatic["CLPROTOCOL_GENTL_SERIAL"] = str(platform / "missing-serial.so")
    run(automatic, "info", "--json", expect=1)
    print("Discovered through GENICAM_CLPROTOCOL and CLAllSerial without a TSV or serial override")
