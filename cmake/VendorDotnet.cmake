set(DOTNET_VERSION "9.0.4" CACHE STRING "Pinned .NET hosting version")

# Detect the NuGet runtime identifier for the current platform
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(DOTNET_RID "linux-arm64")
    else()
        set(DOTNET_RID "linux-x64")
    endif()
    set(DOTNET_LIB_NAME "libnethost.a")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(DOTNET_RID "osx-arm64")
    else()
        set(DOTNET_RID "osx-x64")
    endif()
    set(DOTNET_LIB_NAME "libnethost.a")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(DOTNET_RID "win-x64")
    else()
        set(DOTNET_RID "win-x86")
    endif()
    set(DOTNET_LIB_NAME "nethost.lib")
else()
    message(FATAL_ERROR "Unsupported platform for .NET hosting: ${CMAKE_SYSTEM_NAME}")
endif()

set(DOTNET_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dotnet/native")
set(DOTNET_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dotnet/include")
set(DOTNET_LIB_PATH "${DOTNET_NATIVE_DIR}/${DOTNET_LIB_NAME}")

# Download + extract only if the lib isn't already present
if(NOT EXISTS "${DOTNET_LIB_PATH}")
    string(TOLOWER "microsoft.netcore.app.host.${DOTNET_RID}" DOTNET_PKG_NAME)
    set(DOTNET_NUPKG_URL
            "https://api.nuget.org/v3-flatcontainer/${DOTNET_PKG_NAME}/${DOTNET_VERSION}/${DOTNET_PKG_NAME}.${DOTNET_VERSION}.nupkg"
    )
    set(DOTNET_NUPKG_PATH "${CMAKE_BINARY_DIR}/_dotnet_host/${DOTNET_PKG_NAME}.${DOTNET_VERSION}.nupkg")

    message(STATUS "[Honey] Downloading .NET ${DOTNET_VERSION} hosting libs for ${DOTNET_RID}...")
    file(DOWNLOAD "${DOTNET_NUPKG_URL}" "${DOTNET_NUPKG_PATH}"
            STATUS DOWNLOAD_STATUS
            SHOW_PROGRESS
    )
    list(GET DOWNLOAD_STATUS 0 DOWNLOAD_STATUS_CODE)
    if(NOT DOWNLOAD_STATUS_CODE EQUAL 0)
        list(GET DOWNLOAD_STATUS 1 DOWNLOAD_ERROR)
        message(FATAL_ERROR "[Honey] Failed to download .NET hosting package: ${DOWNLOAD_ERROR}")
    endif()

    message(STATUS "[Honey] Extracting .NET hosting libs...")
    file(ARCHIVE_EXTRACT
            INPUT "${DOTNET_NUPKG_PATH}"
            DESTINATION "${CMAKE_BINARY_DIR}/_dotnet_host/extracted"
    )

    file(MAKE_DIRECTORY "${DOTNET_NATIVE_DIR}")

    # Copy the static lib into vendor/dotnet/native/
    file(COPY
            "${CMAKE_BINARY_DIR}/_dotnet_host/extracted/runtimes/${DOTNET_RID}/native/${DOTNET_LIB_NAME}"
            DESTINATION "${DOTNET_NATIVE_DIR}"
    )

    message(STATUS "[Honey] .NET hosting libs ready at ${DOTNET_NATIVE_DIR}")
endif()

# Download the three hosting headers from dotnet/runtime if not already present.
# These are not reliably included in the nupkg on all platforms, so we fetch them
# directly from the matching release tag to avoid extraction permission issues.
set(_DOTNET_RUNTIME_TAG "v${DOTNET_VERSION}")
set(_DOTNET_HEADER_BASE "https://raw.githubusercontent.com/dotnet/runtime/${_DOTNET_RUNTIME_TAG}/src/native/corehost")
set(_DOTNET_HEADERS
    "nethost/nethost.h"
    "hostfxr.h"
    "coreclr_delegates.h"
)

file(MAKE_DIRECTORY "${DOTNET_INCLUDE_DIR}")

foreach(_hdr IN LISTS _DOTNET_HEADERS)
    get_filename_component(_hdr_name "${_hdr}" NAME)
    set(_hdr_dest "${DOTNET_INCLUDE_DIR}/${_hdr_name}")
    if(NOT EXISTS "${_hdr_dest}")
        message(STATUS "[Honey] Downloading .NET header: ${_hdr_name}")
        file(DOWNLOAD "${_DOTNET_HEADER_BASE}/${_hdr}" "${_hdr_dest}"
                STATUS _HDR_STATUS
        )
        list(GET _HDR_STATUS 0 _HDR_STATUS_CODE)
        if(NOT _HDR_STATUS_CODE EQUAL 0)
            list(GET _HDR_STATUS 1 _HDR_ERROR)
            message(FATAL_ERROR "[Honey] Failed to download ${_hdr_name}: ${_HDR_ERROR}")
        endif()
    endif()
endforeach()

# Expose an imported target: Honey::DotnetHost
add_library(Honey::DotnetHost STATIC IMPORTED GLOBAL)
set_target_properties(Honey::DotnetHost PROPERTIES
        IMPORTED_LOCATION             "${DOTNET_LIB_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${DOTNET_INCLUDE_DIR}"
)

# Linux needs dl and pthread for the hosting APIs
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_property(TARGET Honey::DotnetHost APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES dl pthread
    )
endif()