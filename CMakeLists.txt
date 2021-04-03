cmake_minimum_required(VERSION 3.5)

project(f400_recorder)


# -DCMAKE_EXPORT_COMPILE_COMMANDS=1
# output compile_json.txt
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)



# mmp libs
set(MPP_PATH /media/luoyang/Workspace/Hi3531DV100_SDK_V2.0.4.0/mpp)

# ireader libs path
set(SDK_PATH /media/luoyang/Workspace/projects_learn/libs/demos/rtsp/ireader-rtsp/sdk)
set(MEDIA_PATH /media/luoyang/Workspace/projects_learn/libs/demos/rtsp/ireader-rtsp/media-server)


set(HU_RECORDER /home/luoyang/Dev/hisi/f400-recorder-hu)

# Create a sources variable with link to all c files to compile
# set(SOURCES
#     main.c
#     mouse_ico.c
#     )

aux_source_directory(${PROJECT_SOURCE_DIR}/src APP_SRCS)


set(POMP_SRCS
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/libpomp/src/pomp_buffer.c
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/libpomp/src/pomp_conn.c
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/libpomp/src/pomp_ctx.c
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/libpomp/src/pomp_evt.c
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/libpomp/src/pomp_loop.c
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/libpomp/src/pomp_log.c
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/libpomp/src/pomp_timer.c
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/libpomp/src/pomp_prot.c
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/libpomp/src/pomp_msg.c
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/libpomp/src/pomp_decoder.c
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/libpomp/src/pomp_encoder.c
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/ulog/libulog/ulog_write_android.c
    /home/luoyang/workspace.lnk/projects_learn/Parrot-Developers/ulog/libulog/ulog_write.c
)



set(SOURCES
    /home/luoyang/Dev/hisi/f400-recorder-hu/main/file_copy.cpp
    /home/luoyang/Dev/hisi/f400-recorder-hu/main/hdmi_vi_info.c
    /home/luoyang/Dev/hisi/f400-recorder-hu/main/minIni.c
    /home/luoyang/Dev/hisi/f400-recorder-hu/main/net_control.cpp
    /home/luoyang/Dev/hisi/f400-recorder-hu/main/net_control_client.c
    )

# Include header files
include_directories(
    ${PROJECT_SOURCE_DIR}/inc
    /home/luoyang/Dev/hisi/f400-recorder-hu/main
    /home/luoyang/Dev/hisi/f400-recorder-hu/ffmpeg/include
    /home/luoyang/Dev/hisi/f400-recorder-hu/hisi3531d/include
    /home/luoyang/Dev/hisi/f400-recorder-hu/pub/include
    /home/luoyang/Dev/hisi/f400-recorder-hu/player
    /home/luoyang/Dev/hisi/f400-recorder-hu/encoder
    /home/luoyang/Dev/hisi/f400-recorder-hu/WebServer/app
    /home/luoyang/Dev/hisi/f400-recorder-hu/jsoncpp/include
    /home/luoyang/Dev/cross/hi3531d_v600/nanomsg/include
    )

# link libs path
link_directories(
    ${MPP_PATH}/lib
    /home/luoyang/Dev/hisi/f400-recorder-hu/lib
    /home/luoyang/Dev/hisi/f400-recorder-hu/encoder/lib
    /home/luoyang/Dev/hisi/f400-recorder-hu/ffmpeg/lib
    /home/luoyang/Dev/hisi/f400-recorder-hu/main/bin
    /home/luoyang/Dev/hisi/f400-recorder-hu/hisi3531d/lib
    /home/luoyang/Dev/hisi/f400-recorder-hu/pub/lib
    /home/luoyang/Dev/hisi/f400-recorder-hu/player/lib
    /home/luoyang/Dev/hisi/f400-recorder-hu/jsoncpp/lib
    /home/luoyang/Dev/cross/hi3531d_v600/nanomsg/lib
    )
    


############################################################
# Create a library
############################################################

#Generate the static library from the library sources
# add_library(${PROJECT_NAME} STATIC
#     ${SOURCES} ${CSRCS}
#     )

# target_include_directories(${PROJECT_NAME}
#     PUBLIC
#         ${PROJECT_SOURCE_DIR}
#         )


############################################################
# Create an executable
############################################################

# Add an executable with the above sources
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${APP_SRCS}
    )

# link the new hello_library target with the hello_binary target
target_link_libraries( ${PROJECT_NAME}
    PRIVATE
    dl
    pthread
    rt
    m
    mpi
    hdmi
    VoiceEngine
    jpeg
    aacenc
    aacdec
    upvqe
    dnvqe
    Encoder
    player
    jsoncpp
    pub
    avfilter
    avformat
    avcodec
    swscale
    swresample
    avutil
    x264
    x265
    fdk-aac
    nanomsg
    nsl
    himpp
    png
    z
    )
