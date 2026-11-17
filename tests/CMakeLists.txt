option(CLPROTOCOL_GENTL_TEST_IRIS "Test the producer with iris-gentl" ON)
set(primary_fixture_PROVIDER "" CACHE FILEPATH "Built primary_fixture CLProtocol library for integration tests")
if(primary_fixture_PROVIDER AND NOT EXISTS "${primary_fixture_PROVIDER}")
    message(FATAL_ERROR "primary_fixture_PROVIDER does not name a built CLProtocol driver")
endif()

foreach(serial_mock mock-clserial mock-clallserial)
    add_library(${serial_mock} MODULE mock-clserial.cpp)
    target_compile_features(${serial_mock} PRIVATE cxx_std_20)
    target_link_libraries(${serial_mock} PRIVATE Threads::Threads)
    target_include_directories(${serial_mock} PRIVATE "${CLPROTOCOL_INCLUDE_DIR}")
endforeach()
target_compile_definitions(mock-clallserial PRIVATE MOCK_CLALLSERIAL)

add_library(mock-clprotocol MODULE mock-clprotocol.cpp)
target_compile_features(mock-clprotocol PRIVATE cxx_std_17)
target_compile_definitions(mock-clprotocol PRIVATE CLPROTOCOL_EXPORTS CLPROTOCOL_PLATFORM="${CLPROTOCOL_PLATFORM}")
target_include_directories(mock-clprotocol PRIVATE "${CLPROTOCOL_INCLUDE_DIR}")
set(registration_root "${CMAKE_CURRENT_BINARY_DIR}/registered")
set_target_properties(mock-clprotocol PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${registration_root}/${CLPROTOCOL_PLATFORM}")
add_custom_command(TARGET mock-clallserial POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${registration_root}/${CLPROTOCOL_PLATFORM}"
    COMMAND "${CMAKE_COMMAND}" -E create_symlink "$<TARGET_FILE:mock-clallserial>"
        "${registration_root}/${CLPROTOCOL_PLATFORM}/libCLAllSerial_gcc8_v3_5.so")
set(mock_xml "SchemaVersion.1.1@FixtureVendor#Family#NoSerial@XMLVersion.1.0.0.xml")
configure_file("${mock_xml}" "registered/${mock_xml}" COPYONLY)

if(primary_fixture_PROVIDER)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ports.tsv"
    CONTENT "0\t${primary_fixture_PROVIDER}\tFixtureVendor#primary_fixture#primary_fixture\n1\t$<TARGET_FILE:mock-clprotocol>\tFixtureVendor#Family#NoSerial\tCameraPort\tbig\n")
    set(port_config "${CMAKE_CURRENT_BINARY_DIR}/ports.tsv")
endif()
if(CLPROTOCOL_GENTL_TEST_IRIS)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    find_program(IRIS_GENTL iris-gentl REQUIRED)
    add_test(NAME iris-cli COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-cli.py"
        "${IRIS_GENTL}" "$<TARGET_FILE:clprotocol-gentl>" "$<TARGET_FILE:mock-clserial>"
        "$<TARGET_FILE:mock-clallserial>" "$<TARGET_FILE:mock-clprotocol>" "${port_config}" "${CLPROTOCOL_PLATFORM}")
    set_tests_properties(iris-cli PROPERTIES TIMEOUT 40)
endif()

add_executable(test-control-only test-control-only.cpp)
target_compile_features(test-control-only PRIVATE cxx_std_17)
target_link_libraries(test-control-only PRIVATE GenTLHeaders::GenTLHeaders ${CMAKE_DL_LIBS})
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mock-ports.tsv"
    CONTENT "1\t$<TARGET_FILE:mock-clprotocol>\tFixtureVendor#Family#NoSerial\tCameraPort\tbig\n")
if(port_config)
    set(control_only_devices CLSerial-0 CLSerial-1)
else()
    set(port_config "${CMAKE_CURRENT_BINARY_DIR}/mock-ports.tsv")
    set(control_only_devices CLSerial-1)
endif()
add_test(NAME control-only COMMAND "${CMAKE_COMMAND}" -E env
    "CLPROTOCOL_GENTL_CONFIG=${port_config}"
    "CLPROTOCOL_GENTL_SERIAL=$<TARGET_FILE:mock-clserial>"
    ${CMAKE_CROSSCOMPILING_EMULATOR} "$<TARGET_FILE:test-control-only>" "$<TARGET_FILE:clprotocol-gentl>" ${control_only_devices})
set_tests_properties(control-only PROPERTIES TIMEOUT 20)
add_test(NAME registration COMMAND "${CMAKE_COMMAND}" -E env
    --unset=CLPROTOCOL_GENTL_CONFIG --unset=CLPROTOCOL_GENTL_SERIAL
    --unset=CLPROTOCOL_GENTL_DEVICE_TEMPLATE --unset=CLP_MOCK_SINGLE_PORT
    "CLP_MOCK_SINGLE_TEMPLATE=1" "CLP_MOCK_XML_FILE_ONLY=1"
    "GENICAM_CLPROTOCOL=${registration_root}"
    ${CMAKE_CROSSCOMPILING_EMULATOR} "$<TARGET_FILE:test-control-only>" "$<TARGET_FILE:clprotocol-gentl>" CLSerial-1)
set_tests_properties(registration PROPERTIES TIMEOUT 20)
