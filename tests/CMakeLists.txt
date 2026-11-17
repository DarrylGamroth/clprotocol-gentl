option(CLPROTOCOL_GENTL_TEST_IRIS "Test the producer with iris-gentl" ON)
foreach(serial_mock mock-clserial mock-clallserial)
    add_library(${serial_mock} MODULE mock-clserial.cpp)
    target_compile_features(${serial_mock} PRIVATE cxx_std_20)
    target_link_libraries(${serial_mock} PRIVATE Threads::Threads)
    target_include_directories(${serial_mock} PRIVATE "${CLPROTOCOL_INCLUDE_DIR}")
endforeach()
target_compile_definitions(mock-clallserial PRIVATE MOCK_CLALLSERIAL)

foreach(protocol_mock mock-clprotocol mock-clprotocol-with-serial)
    add_library(${protocol_mock} MODULE mock-clprotocol.cpp)
    target_compile_features(${protocol_mock} PRIVATE cxx_std_17)
    target_compile_definitions(${protocol_mock} PRIVATE CLPROTOCOL_EXPORTS CLPROTOCOL_PLATFORM="${CLPROTOCOL_PLATFORM}")
    target_include_directories(${protocol_mock} PRIVATE "${CLPROTOCOL_INCLUDE_DIR}")
endforeach()
target_compile_definitions(mock-clprotocol-with-serial PRIVATE CLP_MOCK_WITH_SERIAL)
set(registration_root "${CMAKE_CURRENT_BINARY_DIR}/registered")
set_target_properties(mock-clprotocol PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${registration_root}/${CLPROTOCOL_PLATFORM}")
add_custom_command(TARGET mock-clallserial POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${registration_root}/${CLPROTOCOL_PLATFORM}"
    COMMAND "${CMAKE_COMMAND}" -E create_symlink "$<TARGET_FILE:mock-clallserial>"
        "${registration_root}/${CLPROTOCOL_PLATFORM}/libCLAllSerial_gcc8_v3_5.so")
set(mock_xml "SchemaVersion.1.1@FixtureVendor#Family#NoSerial@XMLVersion.1.0.0.xml")
configure_file("${mock_xml}" "registered/${mock_xml}" COPYONLY)

set(paired_root "${CMAKE_CURRENT_BINARY_DIR}/paired")
set_target_properties(mock-clprotocol-with-serial PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${paired_root}/${CLPROTOCOL_PLATFORM}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${mock_xml}" mock_xml_content)
string(REPLACE "NoSerial" "WithSerial" mock_xml_with_serial "${mock_xml}")
string(REPLACE "NoSerial" "WithSerial" mock_xml_content "${mock_xml_content}")
file(GENERATE OUTPUT "${paired_root}/${mock_xml_with_serial}" CONTENT "${mock_xml_content}")

set(port_config "${CMAKE_CURRENT_BINARY_DIR}/ports.tsv")
file(GENERATE OUTPUT "${port_config}"
    CONTENT "0\t$<TARGET_FILE:mock-clprotocol-with-serial>\tFixtureVendor#Family#WithSerial\tCameraPort\tbig\n1\t$<TARGET_FILE:mock-clprotocol>\tFixtureVendor#Family#NoSerial\tCameraPort\tbig\n")
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
add_test(NAME control-only COMMAND "${CMAKE_COMMAND}" -E env
    "CLPROTOCOL_GENTL_CONFIG=${port_config}"
    "CLPROTOCOL_GENTL_SERIAL=$<TARGET_FILE:mock-clserial>"
    ${CMAKE_CROSSCOMPILING_EMULATOR} "$<TARGET_FILE:test-control-only>" "$<TARGET_FILE:clprotocol-gentl>" CLSerial-0 CLSerial-1)
set_tests_properties(control-only PROPERTIES TIMEOUT 20)
add_test(NAME registration COMMAND "${CMAKE_COMMAND}" -E env
    --unset=CLPROTOCOL_GENTL_CONFIG --unset=CLPROTOCOL_GENTL_SERIAL
    --unset=CLPROTOCOL_GENTL_DEVICE_TEMPLATE --unset=CLP_MOCK_SINGLE_PORT
    "CLP_MOCK_SINGLE_TEMPLATE=1" "CLP_MOCK_XML_FILE_ONLY=1"
    "GENICAM_CLPROTOCOL=${registration_root}"
    ${CMAKE_CROSSCOMPILING_EMULATOR} "$<TARGET_FILE:test-control-only>" "$<TARGET_FILE:clprotocol-gentl>" CLSerial-1)
set_tests_properties(registration PROPERTIES TIMEOUT 20)
