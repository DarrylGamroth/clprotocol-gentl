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


iris, cti, mock_serial, config = sys.argv[1:]
env = os.environ | {"CLPROTOCOL_GENTL_CONFIG": config, "CLPROTOCOL_GENTL_SERIAL": mock_serial}
for name in ("CLP_XML_PATH", "primary_fixture_SIM_HARDWARE_ID", "primary_fixture_SIM_NO_CAMERA", "CLP_MOCK_XML_FILE_ONLY"):
    env.pop(name, None)
with TemporaryDirectory(prefix="clprotocol-gentl XML ") as tmp:
    env["TMPDIR"] = tmp
    found = json_lines(run(env, "info", "--json"))
    assert len(found) == 2 and {e["cti"] for e in found} == {cti}, found
    assert found[0]["model"] == "primary_fixture" and found[0]["serial"] == "primary_fixture-TEST-001", found
    assert found[1]["model"] == "NoSerial" and not found[1].get("serial"), found
    print("One CTI discovered both libraries, including a DeviceID without a serial token")
    verify_camera(env, 0, "primary_fixture-TEST-001", "Device")
    verify_camera(env, 1, "ALT-TEST-001", "CameraPort")
    assert not list(Path(tmp).iterdir()), "XML files leaked after device close"

    lines = Path(config).read_text().splitlines()
    fields = lines[1].split("\t")
    driver = Path(fields[1])
    fields[1] = driver.name
    registered = Path(tmp) / "registered.tsv"
    registered.write_text("\t".join(fields) + "\n")
    registration = Path(tmp) / "CLProtocol"
    platform = registration / "Linux64_x64"
    platform.mkdir(parents=True)
    # Keep the library target apart from its XML to test the registration root.
    library_only = Path(tmp) / "lib"
    library_only.mkdir()
    driver_copy = library_only / driver.name
    shutil.copyfile(driver, driver_copy)
    (platform / driver.name).symlink_to(driver_copy)
    for xml_file in driver.parent.parent.glob("*.xml"):
        shutil.copyfile(xml_file, registration / xml_file.name)
    env["CLPROTOCOL_GENTL_CONFIG"] = str(registered)
    env["GENICAM_CLPROTOCOL"] = str(registration)
    found = json_lines(run(env, "info", "--json"))
    assert len(found) == 1 and found[0]["device"] == "CLSerial-1", found
    verify_camera(env | {"CLP_MOCK_XML_FILE_ONLY": "1"}, 1, "ALT-TEST-001", "CameraPort")
    print("Resolved the second library through a registration symlink and loaded its installed XML")

    auto_root = Path(tmp) / "automatic"
    auto_platform = auto_root / "Linux64_x64"
    auto_platform.mkdir(parents=True)
    protocol = Path(lines[0].split("\t")[1])
    (auto_platform / protocol.name).symlink_to(protocol)
    (auto_platform / "libCLAllSerial_gcc8_v3_5.so").symlink_to(Path(mock_serial).resolve())
    automatic = env | {"GENICAM_CLPROTOCOL": str(auto_root), "CLP_MOCK_SINGLE_PORT": "1"}
    for name in ("CLPROTOCOL_GENTL_CONFIG", "CLPROTOCOL_GENTL_SERIAL",
                 "CLPROTOCOL_GENTL_DEVICE_TEMPLATE"):
        automatic.pop(name, None)
    found = json_lines(run(automatic, "info", "--json"))
    assert len(found) == 1 and found[0]["serial"] == "primary_fixture-TEST-001", found
    verify_camera(automatic, 0, "primary_fixture-TEST-001", "Device")

    # Installed drivers for different cameras require a selection before probing.
    automatic["GENICAM_CLPROTOCOL"] += ";" + str(registration)
    run(automatic, "info", "--json", expect=1)
    automatic["CLPROTOCOL_GENTL_DEVICE_TEMPLATE"] = "FixtureVendor#primary_fixture#primary_fixture"
    selected = json_lines(run(automatic, "info", "--json"))
    assert selected == found, selected
    print("Discovered through GENICAM_CLPROTOCOL and CLAllSerial without a TSV or serial override")
