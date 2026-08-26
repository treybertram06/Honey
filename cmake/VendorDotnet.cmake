set(DOTNET_VERSION "10.0.8" CACHE STRING "Pinned .NET hosting version")

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
    set(DOTNET_DLL_NAME "nethost.dll")
else()
    message(FATAL_ERROR "Unsupported platform for .NET hosting: ${CMAKE_SYSTEM_NAME}")
endif()

set(DOTNET_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dotnet/native")
set(DOTNET_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dotnet/include")
set(DOTNET_LIB_PATH "${DOTNET_NATIVE_DIR}/${DOTNET_LIB_NAME}")
if(DEFINED DOTNET_DLL_NAME)
    set(DOTNET_DLL_PATH "${DOTNET_NATIVE_DIR}/${DOTNET_DLL_NAME}")
endif()

# Download + extract only if the lib (and, on Windows, the DLL) isn't already present
if(NOT EXISTS "${DOTNET_LIB_PATH}" OR (DEFINED DOTNET_DLL_PATH AND NOT EXISTS "${DOTNET_DLL_PATH}"))
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

    # On Windows, nethost is a DLL (nethost.lib is just the import lib) — copy it too
    if(DEFINED DOTNET_DLL_NAME)
        file(COPY
                "${CMAKE_BINARY_DIR}/_dotnet_host/extracted/runtimes/${DOTNET_RID}/native/${DOTNET_DLL_NAME}"
                DESTINATION "${DOTNET_NATIVE_DIR}"
        )
    endif()

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

# ------------------------------------------------------------------------------
# Bundle the full .NET runtime so no system installation is required at runtime
# ------------------------------------------------------------------------------
set(DOTNET_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/dotnet/runtime")

if(NOT EXISTS "${DOTNET_RUNTIME_DIR}/host")
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_DOTNET_ARCHIVE_EXT "zip")
    else()
        set(_DOTNET_ARCHIVE_EXT "tar.gz")
    endif()

    set(_DOTNET_ARCHIVE "dotnet-runtime-${DOTNET_VERSION}-${DOTNET_RID}.${_DOTNET_ARCHIVE_EXT}")
    set(_DOTNET_RUNTIME_URL
        "https://dotnetcli.azureedge.net/dotnet/Runtime/${DOTNET_VERSION}/${_DOTNET_ARCHIVE}"
    )
    set(_DOTNET_RUNTIME_ARCHIVE "${CMAKE_BINARY_DIR}/_dotnet_host/${_DOTNET_ARCHIVE}")

    message(STATUS "[Honey] Downloading bundled .NET ${DOTNET_VERSION} runtime for ${DOTNET_RID}...")
    file(DOWNLOAD "${_DOTNET_RUNTIME_URL}" "${_DOTNET_RUNTIME_ARCHIVE}"
        STATUS _RT_STATUS
        SHOW_PROGRESS
    )
    list(GET _RT_STATUS 0 _RT_CODE)
    if(NOT _RT_CODE EQUAL 0)
        list(GET _RT_STATUS 1 _RT_ERR)
        message(FATAL_ERROR "[Honey] Failed to download .NET runtime: ${_RT_ERR}")
    endif()

    message(STATUS "[Honey] Extracting .NET runtime to vendor/dotnet/runtime/...")
    file(MAKE_DIRECTORY "${DOTNET_RUNTIME_DIR}")
    file(ARCHIVE_EXTRACT
        INPUT       "${_DOTNET_RUNTIME_ARCHIVE}"
        DESTINATION "${DOTNET_RUNTIME_DIR}"
    )
    message(STATUS "[Honey] Bundled .NET runtime ready at ${DOTNET_RUNTIME_DIR}")
endif()

# ------------------------------------------------------------------------------
# Expose an imported target: Honey::DotnetHost
# ------------------------------------------------------------------------------
# On Windows, nethost is a DLL (nethost.lib is only the import lib), so the
# target must be SHARED IMPORTED with both IMPORTED_IMPLIB and IMPORTED_LOCATION
# set — otherwise $<TARGET_RUNTIME_DLLS:...> won't know to copy nethost.dll
# next to the exe. On Linux/macOS, libnethost.a is genuinely static.
if(DEFINED DOTNET_DLL_PATH)
    add_library(Honey::DotnetHost SHARED IMPORTED GLOBAL)
    set_target_properties(Honey::DotnetHost PROPERTIES
            IMPORTED_IMPLIB               "${DOTNET_LIB_PATH}"
            IMPORTED_LOCATION              "${DOTNET_DLL_PATH}"
            INTERFACE_INCLUDE_DIRECTORIES "${DOTNET_INCLUDE_DIR}"
    )
else()
    add_library(Honey::DotnetHost STATIC IMPORTED GLOBAL)
    set_target_properties(Honey::DotnetHost PROPERTIES
            IMPORTED_LOCATION             "${DOTNET_LIB_PATH}"
            INTERFACE_INCLUDE_DIRECTORIES "${DOTNET_INCLUDE_DIR}"
    )
endif()

# Bake the absolute runtime root path in so dotnet_host.cpp can find libhostfxr
# without relying on a system .NET installation.
# For shipped games this should be replaced with a path relative to the executable.
set_property(TARGET Honey::DotnetHost APPEND PROPERTY
        INTERFACE_COMPILE_DEFINITIONS
        HN_DOTNET_RUNTIME_ROOT="${DOTNET_RUNTIME_DIR}"
)

# Linux needs dl and pthread for the hosting APIs
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_property(TARGET Honey::DotnetHost APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES dl pthread
    )
endif()

