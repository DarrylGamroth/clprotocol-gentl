cmake_minimum_required(VERSION 3.20)
project(clprotocol_gentl VERSION 0.1.0 LANGUAGES CXX)
option(BUILD_TESTING "Build the CLProtocol and Iris simulator tests" OFF)
include(CTest)
find_package(Threads REQUIRED)

include(cmake/EmvaCLProtocol.cmake)
set(GENTL_PRODUCER_CORE_SOURCE_DIR "" CACHE PATH
    "Path to a GenTLProducerCore source checkout in place of an installed package")
set(GENTL_INCLUDE_DIR "" CACHE PATH "Optional directory containing the EMVA GenTL.h")
if(GENTL_INCLUDE_DIR AND NOT GenTLHeaders_ROOT)
    set(GenTLHeaders_ROOT "${GENTL_INCLUDE_DIR}" CACHE PATH "Root containing the EMVA GenTL.h" FORCE)
endif()
if(NOT GENTL_PRODUCER_CORE_SOURCE_DIR)
    find_package(GenTLProducerCore 0.2.5 CONFIG QUIET)
endif()
if(NOT TARGET GenTLProducerCore::gentl_producer_core)
    include(FetchContent)
    FetchContent_Declare(GenTLProducerCore
        GIT_REPOSITORY ssh://git@github.com/DarrylGamroth/GenTLProducerCore.git
        GIT_TAG 26a3b9f1c99a50928a29f3c336e7fdb2d7e4bda3)
    function(add_gentl_producer_core)
        set(BUILD_TESTING OFF)
        if(GENTL_PRODUCER_CORE_SOURCE_DIR)
            add_subdirectory("${GENTL_PRODUCER_CORE_SOURCE_DIR}"
                             "${CMAKE_CURRENT_BINARY_DIR}/gentl_producer_core" EXCLUDE_FROM_ALL)
        else()
            FetchContent_MakeAvailable(GenTLProducerCore)
        endif()
    endfunction()
    add_gentl_producer_core()
endif()

add_library(clprotocol-gentl MODULE
    src/config.cpp src/producer.cpp src/protocol.cpp src/no-stream.cpp src/xml.cpp)
set_target_properties(clprotocol-gentl PROPERTIES
    PREFIX "" SUFFIX ".cti" CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(clprotocol-gentl PRIVATE "${CLPROTOCOL_INCLUDE_DIR}")
target_compile_options(clprotocol-gentl PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(clprotocol-gentl PRIVATE
    GenTLProducerCore::gentl_producer_core Threads::Threads ${CMAKE_DL_LIBS})
include(GNUInstallDirs)
install(TARGETS clprotocol-gentl LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/gentl")

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
