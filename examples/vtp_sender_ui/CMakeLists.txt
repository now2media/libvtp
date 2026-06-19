cmake_minimum_required(VERSION 3.16)
project(vtp_sender_ui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

# Find FFmpeg components
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)

add_executable(vtp_sender_ui
    main.cpp
    SenderWindow.cpp
    SenderWindow.h
)

target_include_directories(vtp_sender_ui PRIVATE
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
    ${SWRESAMPLE_INCLUDE_DIRS}
)

if(TARGET vtp)
    # Subdirectory build (part of main project)
    target_link_libraries(vtp_sender_ui PRIVATE
        Qt6::Widgets
        Qt6::Multimedia
        vtp
        ${AVCODEC_LIBRARIES}
        ${AVFORMAT_LIBRARIES}
        ${AVUTIL_LIBRARIES}
        ${SWSCALE_LIBRARIES}
        ${SWRESAMPLE_LIBRARIES}
    )
else()
    # Standalone build (user zips and builds individually)
    find_path(VTP_INCLUDE_DIR vtp.h PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
    find_library(VTP_LIBRARY NAMES vtp PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../build")

    if(NOT VTP_INCLUDE_DIR OR NOT VTP_LIBRARY)
        message(FATAL_ERROR "VTP SDK (vtp.h and libvtp.so) is required but not found. Please compile the main VTP project first.")
    endif()

    target_include_directories(vtp_sender_ui PRIVATE
        ${VTP_INCLUDE_DIR}
    )

    target_link_libraries(vtp_sender_ui PRIVATE
        Qt6::Widgets
        Qt6::Multimedia
        ${VTP_LIBRARY}
        ${AVCODEC_LIBRARIES}
        ${AVFORMAT_LIBRARIES}
        ${AVUTIL_LIBRARIES}
        ${SWSCALE_LIBRARIES}
        ${SWRESAMPLE_LIBRARIES}
    )

    add_custom_command(TARGET vtp_sender_ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${VTP_LIBRARY}
        $<TARGET_FILE_DIR:vtp_sender_ui>
    )
endif()
