cmake_minimum_required(VERSION 3.15)
project(vtp VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

option(VTP_BUILD_EXAMPLES "Build Qt6 UI examples" ON)
option(VTP_BUILD_TOOLS "Build CLI diagnostic tools" ON)
option(VTP_ENABLE_OPUS "Enable Opus audio compression if libopus is installed" ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Threads REQUIRED)

find_path(ASIO_INCLUDE_DIR asio.hpp
    PATHS
        "${CMAKE_CURRENT_BINARY_DIR}/_deps/asio-src/asio/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/build/_deps/asio-src/asio/include"
        "/usr/include"
        "/usr/local/include"
)

if(NOT ASIO_INCLUDE_DIR)
    include(FetchContent)
    FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-30-2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(asio)
    set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include")
endif()
message(STATUS "Using Asio headers: ${ASIO_INCLUDE_DIR}")

find_path(TURBOJPEG_INCLUDE_DIR turbojpeg.h)
find_library(TURBOJPEG_LIBRARY NAMES turbojpeg)

if(NOT TURBOJPEG_INCLUDE_DIR OR NOT TURBOJPEG_LIBRARY)
    message(FATAL_ERROR "libturbojpeg (TurboJPEG) is required but not found.")
endif()

message(STATUS "Found TurboJPEG header: ${TURBOJPEG_INCLUDE_DIR}")
message(STATUS "Found TurboJPEG library: ${TURBOJPEG_LIBRARY}")

set(VTP_OPUS_FOUND FALSE)
if(VTP_ENABLE_OPUS)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(OPUS QUIET opus)
    endif()
    if(OPUS_FOUND)
        set(VTP_OPUS_FOUND TRUE)
    else()
        find_path(OPUS_INCLUDE_DIR opus/opus.h)
        find_library(OPUS_LIBRARY NAMES opus)
        if(OPUS_INCLUDE_DIR AND OPUS_LIBRARY)
            set(OPUS_INCLUDE_DIRS "${OPUS_INCLUDE_DIR}")
            set(OPUS_LIBRARIES "${OPUS_LIBRARY}")
            set(VTP_OPUS_FOUND TRUE)
        endif()
    endif()
endif()

if(VTP_OPUS_FOUND)
    message(STATUS "Found Opus, enabling Opus audio support")
else()
    message(STATUS "Opus not found, building with PCM audio only")
endif()

add_library(vtp SHARED
    src/vtp.cpp
    src/VTPSender.cpp
    src/VTPReceiver.cpp
    src/VTPDiscovery.cpp
    src/VTPLog.cpp
    src/VTPSimd.cpp
    src/VTPAudioCodec.cpp
    src/VTPVideoCodec.cpp
    src/VTPTestPattern.cpp
)

set_target_properties(vtp PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_compile_definitions(vtp PRIVATE
    VTP_EXPORTS
    ASIO_STANDALONE
)

target_include_directories(vtp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(vtp PRIVATE
    src
    ${TURBOJPEG_INCLUDE_DIR}
    ${ASIO_INCLUDE_DIR}
)

target_link_libraries(vtp PRIVATE
    ${TURBOJPEG_LIBRARY}
    Threads::Threads
)

if(VTP_OPUS_FOUND)
    target_compile_definitions(vtp PRIVATE VTP_HAS_OPUS)
    target_include_directories(vtp PRIVATE ${OPUS_INCLUDE_DIRS})
    target_link_libraries(vtp PRIVATE ${OPUS_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(vtp PRIVATE ws2_32 wsock32)
endif()

if(UNIX AND NOT APPLE)
    set_target_properties(vtp PROPERTIES
        LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/vtp.map"
    )
endif()

if(VTP_BUILD_TOOLS)
    add_executable(vtp-discover tools/vtp_discover.cpp)
    target_link_libraries(vtp-discover PRIVATE vtp Threads::Threads)

    add_executable(vtp-bench tools/vtp_bench.cpp)
    target_link_libraries(vtp-bench PRIVATE vtp Threads::Threads)

    install(TARGETS vtp-discover vtp-bench
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(VTP_BUILD_EXAMPLES)
    find_package(Qt6 COMPONENTS Widgets Multimedia QUIET)
    if(Qt6_FOUND)
        message(STATUS "Qt6 found! Including UI examples in build.")
        add_subdirectory(examples/vtp_sender_ui)
        add_subdirectory(examples/vtp_receiver_ui)
    else()
        message(WARNING "Qt6 not found. Skipping UI examples.")
    endif()
endif()

install(TARGETS vtp
    EXPORT vtpTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES include/vtp.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT vtpTargets
    FILE vtpTargets.cmake
    NAMESPACE vtp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vtp
)

configure_package_config_file(
    cmake/vtpConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/vtpConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vtp
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vtpConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/vtpConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/vtpConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vtp
)

configure_file(
    cmake/vtp.pc.in
    "${CMAKE_CURRENT_BINARY_DIR}/vtp.pc"
    @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/vtp.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
