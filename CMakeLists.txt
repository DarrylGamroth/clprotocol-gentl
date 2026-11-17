cmake_minimum_required(VERSION 3.18)
project(clprotocol_gentl VERSION 0.1.0 LANGUAGES CXX)
option(BUILD_TESTING "Build the CLProtocol and Iris simulator tests" OFF)
include(CTest)
find_package(Threads REQUIRED)

find_path(GENTL_INCLUDE_DIR GenTL.h REQUIRED)
find_path(CLPROTOCOL_INCLUDE_DIR CLProtocol/CLProtocol.h REQUIRED)

add_library(clprotocol-gentl MODULE
    src/config.cpp src/producer.cpp src/protocol.cpp src/no-stream.cpp src/xml.cpp)
set_target_properties(clprotocol-gentl PROPERTIES
    PREFIX "" SUFFIX ".cti" CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_compile_features(clprotocol-gentl PRIVATE cxx_std_17)
target_include_directories(clprotocol-gentl PRIVATE "${GENTL_INCLUDE_DIR}" "${CLPROTOCOL_INCLUDE_DIR}")
target_compile_options(clprotocol-gentl PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(clprotocol-gentl PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
include(GNUInstallDirs)
install(TARGETS clprotocol-gentl LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/gentl")

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
