if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "CLProtocol GenTL currently requires Linux")
endif()

# Match the Linux directories and SDK archive names in EMVA GenICam 3.5.1.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" clprotocol_processor)
if(clprotocol_processor MATCHES "^(x86_64|amd64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CLPROTOCOL_PLATFORM Linux64_x64)
elseif(clprotocol_processor MATCHES "^(aarch64|arm64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CLPROTOCOL_PLATFORM Linux64_ARM)
endif()
if(NOT CLPROTOCOL_PLATFORM)
    message(FATAL_ERROR "No EMVA CLProtocol SDK layout for ${CMAKE_SYSTEM_PROCESSOR} with ${CMAKE_SIZEOF_VOID_P}-byte pointers")
endif()

find_path(CLPROTOCOL_INCLUDE_DIR CLProtocol/CLProtocol.h)
if(NOT CLPROTOCOL_INCLUDE_DIR)
    set(emva_dir "${CMAKE_CURRENT_BINARY_DIR}/_deps/emva")
    set(archive "${emva_dir}/GenICam_V3_5_1_public_data.zip")
    set(sdk "${CLPROTOCOL_PLATFORM}/GenICam-CLProtocol-3.5.1-${CLPROTOCOL_PLATFORM}_gcc8-SDK.tar.gz")
    set(clprotocol_sdk "${emva_dir}/clprotocol-3.5.1/${CLPROTOCOL_PLATFORM}")
    set(CLPROTOCOL_INCLUDE_DIR "${clprotocol_sdk}/library/CPP/include")
    if(NOT EXISTS "${CLPROTOCOL_INCLUDE_DIR}/CLProtocol/ClAllSerial.h")
        file(MAKE_DIRECTORY "${emva_dir}" "${clprotocol_sdk}")
        file(DOWNLOAD "https://www.emva.org/wp-content/uploads/GenICam_V3_5_1_public_data.zip"
            "${archive}"
            EXPECTED_HASH SHA256=9ad0b97c7904efbbf37c2f65c547502cb5c369e982b1c1e4d8cce6c6d6d70858
            TLS_VERIFY ON TIMEOUT 180 INACTIVITY_TIMEOUT 30)
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${emva_dir}" PATTERNS "${sdk}")
        file(ARCHIVE_EXTRACT INPUT "${emva_dir}/${sdk}" DESTINATION "${clprotocol_sdk}"
            PATTERNS "library/CPP/include/CLProtocol/*")
    endif()
endif()
