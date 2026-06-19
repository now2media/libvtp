cmake_minimum_required(VERSION 3.16)
project(vtp_receiver_ui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

add_executable(vtp_receiver_ui
    main.cpp
    ReceiverWindow.cpp
    ReceiverWindow.h
    VideoWidget.cpp
    VideoWidget.h
)

if(TARGET vtp)
    # Subdirectory build (part of main project)
    target_link_libraries(vtp_receiver_ui PRIVATE
        Qt6::Widgets
        Qt6::Multimedia
        vtp
    )
else()
    # Standalone build (user zips and builds individually)
    find_path(VTP_INCLUDE_DIR vtp.h PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
    find_library(VTP_LIBRARY NAMES vtp PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../build")

    if(NOT VTP_INCLUDE_DIR OR NOT VTP_LIBRARY)
        message(FATAL_ERROR "VTP SDK (vtp.h and libvtp.so) is required but not found. Please compile the main VTP project first.")
    endif()

    target_include_directories(vtp_receiver_ui PRIVATE
        ${VTP_INCLUDE_DIR}
    )

    target_link_libraries(vtp_receiver_ui PRIVATE
        Qt6::Widgets
        Qt6::Multimedia
        ${VTP_LIBRARY}
    )

    add_custom_command(TARGET vtp_receiver_ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${VTP_LIBRARY}
        $<TARGET_FILE_DIR:vtp_receiver_ui>
    )
endif()
