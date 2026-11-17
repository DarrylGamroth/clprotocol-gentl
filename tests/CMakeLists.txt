find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_program(IRIS_GENTL iris-gentl REQUIRED)
set(primary_fixture_PROVIDER "" CACHE FILEPATH "Built primary_fixture CLProtocol library for integration tests")
if(primary_fixture_PROVIDER AND NOT EXISTS "${primary_fixture_PROVIDER}")
    message(FATAL_ERROR "primary_fixture_PROVIDER does not name a built CLProtocol driver")
endif()

add_library(mock-clserial MODULE mock-clserial.cpp)
target_compile_features(mock-clserial PRIVATE cxx_std_20)
target_link_libraries(mock-clserial PRIVATE Threads::Threads)
target_include_directories(mock-clserial PRIVATE "${CLPROTOCOL_INCLUDE_DIR}")

add_library(mock-clprotocol MODULE mock-clprotocol.cpp)
target_compile_features(mock-clprotocol PRIVATE cxx_std_17)
target_compile_definitions(mock-clprotocol PRIVATE CLPROTOCOL_EXPORTS)
target_include_directories(mock-clprotocol PRIVATE "${CLPROTOCOL_INCLUDE_DIR}")
set_target_properties(mock-clprotocol PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/registered/Linux64_x64")
set(mock_xml "SchemaVersion.1.1@FixtureVendor#Family#NoSerial@XMLVersion.1.0.0.xml")
configure_file("${mock_xml}" "registered/${mock_xml}" COPYONLY)

if(primary_fixture_PROVIDER)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ports.tsv"
    CONTENT "0\t${primary_fixture_PROVIDER}\tFixtureVendor#primary_fixture#primary_fixture\n1\t$<TARGET_FILE:mock-clprotocol>\tFixtureVendor#Family#NoSerial\tCameraPort\tbig\n")
    set(port_config "${CMAKE_CURRENT_BINARY_DIR}/ports.tsv")
endif()
add_test(NAME iris-cli COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-cli.py"
    "${IRIS_GENTL}" "$<TARGET_FILE:clprotocol-gentl>" "$<TARGET_FILE:mock-clserial>"
    "$<TARGET_FILE:mock-clprotocol>" "${port_config}")
set_tests_properties(iris-cli PROPERTIES TIMEOUT 40)

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
    "$<TARGET_FILE:test-control-only>" "$<TARGET_FILE:clprotocol-gentl>" ${control_only_devices})
set_tests_properties(control-only PROPERTIES TIMEOUT 20)
