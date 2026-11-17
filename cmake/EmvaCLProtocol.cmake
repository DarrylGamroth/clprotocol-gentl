find_path(CLPROTOCOL_INCLUDE_DIR CLProtocol/CLProtocol.h)
if(NOT CLPROTOCOL_INCLUDE_DIR)
    set(emva_dir "${CMAKE_CURRENT_BINARY_DIR}/_deps/emva")
    set(archive "${emva_dir}/GenICam_V3_5_1_public_data.zip")
    set(sdk "Linux64_x64/GenICam-CLProtocol-3.5.1-Linux64_x64_gcc8-SDK.tar.gz")
    set(clprotocol_sdk "${emva_dir}/clprotocol-3.5.1")
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
