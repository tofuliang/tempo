cmake_minimum_required(VERSION 3.18.0)

project("tempo-airplay")

set(FFMPEG_DIR ${CMAKE_SOURCE_DIR}/../jniLibs/ffmpeg)
set(LIBAIRPLAY_DIR ${CMAKE_SOURCE_DIR}/libairplay-sender)

include_directories(
    ${LIBAIRPLAY_DIR}/src
    ${LIBAIRPLAY_DIR}/src/core
    ${LIBAIRPLAY_DIR}/src/pal
    ${FFMPEG_DIR}/include
)

foreach(ffmpeg_lib avcodec avformat avutil swresample)
    add_library(${ffmpeg_lib} SHARED IMPORTED)
    set_target_properties(${ffmpeg_lib} PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_DIR}/${ANDROID_ABI}/lib${ffmpeg_lib}.so
    )
endforeach()

add_library(tempo-airplay SHARED
    native-lib.cpp
    ${LIBAIRPLAY_DIR}/src/core/raop.c
    ${LIBAIRPLAY_DIR}/src/core/rtp_stream.c
    ${LIBAIRPLAY_DIR}/src/pal/pal_network.c
)

find_library(log-lib log)

target_link_libraries(tempo-airplay
    avformat
    avcodec
    swresample
    avutil
    ${log-lib}
    z
    m
)
