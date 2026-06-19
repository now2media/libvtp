cmake_minimum_required(VERSION 3.16)

project(LiveDevice VERSION 1.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Use Debug if Qt Creator does not pass BUILD_TYPE
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------------------
# FRAMEWORK_DIR: build/Desktop-Debug folder (3 levels above this CMakeLists.txt)
# Example/LiveDevice/CMakeLists.txt → Example → Desktop-Debug
# -------------------------------------------------------------------
get_filename_component(FRAMEWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SDK" ABSOLUTE)

set(NOW2SDK_LIB       "${FRAMEWORK_DIR}/libnow2sdk.so")
set(NOW2SDK_HEADERS   "${FRAMEWORK_DIR}/header/now2sdk")
set(SDK_DECKLINK  "${FRAMEWORK_DIR}/header/decklink")
set(SDK_NDI_INC   "${FRAMEWORK_DIR}/header/ndi")
set(SDK_NDI_LIB   "${FRAMEWORK_DIR}/libndi.so")

if(NOT EXISTS ${NOW2SDK_LIB})
    message(FATAL_ERROR "libnow2sdk.so not found: ${NOW2SDK_LIB}")
endif()

# --- FFmpeg ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC  REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL   REQUIRED libavutil)
pkg_check_modules(SWSCALE  REQUIRED libswscale)

# --- Qt ---
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS
    Widgets OpenGLWidgets Concurrent OpenGL Multimedia
)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS
    Widgets OpenGLWidgets Concurrent Multimedia
)

# --- Project Files ---
set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
)

# --- Executable ---
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(LiveDevice MANUAL_FINALIZATION ${PROJECT_SOURCES})
else()
    add_executable(LiveDevice ${PROJECT_SOURCES})
endif()

# --- Include directories ---
target_include_directories(LiveDevice PRIVATE
    ${NOW2SDK_HEADERS}   # LFile.h, LLive.h, LSwitcher.h etc.
    ${SDK_DECKLINK}      # DeckLinkAPI.h etc.
    ${SDK_NDI_INC}       # Processing.NDI.*.h
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# --- Link ---
target_link_libraries(LiveDevice PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Multimedia
    ${NOW2SDK_LIB}
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
)

# --- RPATH: automatically find .so files next to the executable ---
set_target_properties(LiveDevice PROPERTIES
    SKIP_BUILD_RPATH FALSE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# -------------------------------------------------------------------
# Post-build: Copy all .so files from build/Desktop-Debug next to the executable
# -------------------------------------------------------------------

# 1. libnow2sdk.so
add_custom_command(TARGET LiveDevice POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${NOW2SDK_LIB}"
    "$<TARGET_FILE_DIR:LiveDevice>"
)

# 2. All .so and .so.* files inside build/Desktop-Debug
add_custom_command(TARGET LiveDevice POST_BUILD
    COMMAND find "${FRAMEWORK_DIR}" -maxdepth 1 -name "*.so*" -exec cp -f {} "$<TARGET_FILE_DIR:LiveDevice>/" \;
    VERBATIM
)

# 3. NDI library (already copied from FRAMEWORK_DIR in step 2, extra safety)
add_custom_command(TARGET LiveDevice POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SDK_NDI_LIB}"
    "$<TARGET_FILE_DIR:LiveDevice>/libndi.so"
)

# --- Install ---
include(GNUInstallDirs)
install(TARGETS LiveDevice
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(LiveDevice)
endif()