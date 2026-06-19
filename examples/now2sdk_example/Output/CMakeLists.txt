cmake_minimum_required(VERSION 3.16)

project(Output VERSION 1.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(FRAMEWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SDK" ABSOLUTE)

set(LML_LIB       "${FRAMEWORK_DIR}/libnow2sdk.so")
set(LML_HEADERS   "${FRAMEWORK_DIR}/header/now2sdk")
set(SDK_DECKLINK  "${FRAMEWORK_DIR}/header/decklink")
set(SDK_NDI_INC   "${FRAMEWORK_DIR}/header/ndi")
set(SDK_NDI_LIB   "${FRAMEWORK_DIR}/libndi.so")

if(NOT EXISTS ${LML_LIB})
    message(FATAL_ERROR "libnow2sdk.so not found: ${LML_LIB}")
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
    qt_add_executable(Output MANUAL_FINALIZATION ${PROJECT_SOURCES})
else()
    add_executable(Output ${PROJECT_SOURCES})
endif()

# --- Include directories ---
target_include_directories(Output PRIVATE
    ${LML_HEADERS}       # LFile.h, LLive.h, LSwitcher.h etc.
    ${SDK_DECKLINK}      # DeckLinkAPI.h etc.
    ${SDK_NDI_INC}       # NDI headers
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# --- Link ---
target_link_libraries(Output PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Multimedia
    ${LML_LIB}
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
)

# --- RPATH ---
set_target_properties(Output PROPERTIES
    SKIP_BUILD_RPATH FALSE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# -------------------------------------------------------------------
# POST_BUILD: Copy library dependencies next to executable
# -------------------------------------------------------------------

# 1. libnow2sdk.so
add_custom_command(TARGET Output POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${LML_LIB}"
    "$<TARGET_FILE_DIR:Output>"
)

# 2. Copy all .so and .so.* files from build/Desktop-Debug
add_custom_command(TARGET Output POST_BUILD
    COMMAND find "${FRAMEWORK_DIR}" -maxdepth 1 -name "*.so*" -exec cp -f {} "$<TARGET_FILE_DIR:Output>/" \;
    VERBATIM
)

# 3. libndi.so
add_custom_command(TARGET Output POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SDK_NDI_LIB}"
    "$<TARGET_FILE_DIR:Output>/libndi.so"
)

# --- Install ---
include(GNUInstallDirs)
install(TARGETS Output
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Output)
endif()
