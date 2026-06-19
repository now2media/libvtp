cmake_minimum_required(VERSION 3.15)
project(vtp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Find threads
find_package(Threads REQUIRED)

# Fetch standalone Asio (Header-only)
include(FetchContent)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(asio)

# Find TurboJPEG
find_path(TURBOJPEG_INCLUDE_DIR turbojpeg.h)
find_library(TURBOJPEG_LIBRARY NAMES turbojpeg)

if(NOT TURBOJPEG_INCLUDE_DIR OR NOT TURBOJPEG_LIBRARY)
    message(FATAL_ERROR "libturbojpeg (TurboJPEG) is required but not found.")
endif()

message(STATUS "Found TurboJPEG header: ${TURBOJPEG_INCLUDE_DIR}")
message(STATUS "Found TurboJPEG library: ${TURBOJPEG_LIBRARY}")

# VTP Library
add_library(vtp SHARED
    src/vtp.cpp
    src/VTPSender.cpp
    src/VTPReceiver.cpp
    src/VTPDiscovery.cpp
)

target_include_directories(vtp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(vtp PRIVATE
    src
    ${TURBOJPEG_INCLUDE_DIR}
    ${asio_SOURCE_DIR}/asio/include
)

target_link_libraries(vtp PRIVATE
    ${TURBOJPEG_LIBRARY}
    Threads::Threads
)

if(UNIX AND NOT APPLE)
    set_target_properties(vtp PROPERTIES
        LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/vtp.map"
    )
endif()



# Integrate GUI Examples if Qt6 is available
find_package(Qt6 COMPONENTS Widgets Multimedia QUIET)
if (Qt6_FOUND)
    message(STATUS "Qt6 found! Including UI examples in build.")
    add_subdirectory(examples/vtp_sender_ui)
    add_subdirectory(examples/vtp_receiver_ui)
else()
    message(WARNING "Qt6 not found. Skipping UI examples.")
endif()
