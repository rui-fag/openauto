cmake_minimum_required(VERSION 4.0.0)
project(openauto CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
else()
    string(TOLOWER "${CMAKE_BUILD_TYPE}" _build_type_lower)

    if(_build_type_lower STREQUAL "release")
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    elseif(_build_type_lower STREQUAL "debug")
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif()
endif()


set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(base_directory ${CMAKE_CURRENT_SOURCE_DIR})
set(resources_directory ${base_directory}/assets)
set(sources_directory ${base_directory}/src)
set(include_directory ${base_directory}/include)

option(OPENAUTO_USE_BUNDLED_AASDK "Build bundled aasdk/ subdirectory as part of the openauto build" ON)
if(OPENAUTO_USE_BUNDLED_AASDK)
    # NOTE: keep this add_subdirectory() before the output-directory
    # settings below so aasdk keeps its historical outputs in aasdk/lib
    # and aasdk/bin (its CMakeLists only sets them if not already set).
    add_subdirectory(aasdk)
    # aasdk/aasdk_proto are plain C++/protobuf libraries with no Qt
    # sources. They inherit this directory's CMAKE_AUTOMOC/AUTOUIC/
    # AUTORCC=ON otherwise, which triggers an "AUTOGEN: No valid Qt
    # version found" author warning during generation. Opt them out.
    foreach(_openauto_noqt_target IN ITEMS aasdk aasdk_proto aasdk_ut)
        if(TARGET ${_openauto_noqt_target})
            set_target_properties(${_openauto_noqt_target} PROPERTIES
                AUTOMOC OFF
                AUTOUIC OFF
                AUTORCC OFF)
        endif()
    endforeach()
    set(AASDK_LIBRARIES aasdk)
    set(AASDK_PROTO_LIBRARIES aasdk_proto)
    set(AASDK_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/aasdk/include")
    # Generated protobuf headers land in the aasdk binary dir. For
    # in-source builds that equals the source dir; list both so
    # out-of-source builds work too.
    set(AASDK_PROTO_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/aasdk" "${CMAKE_CURRENT_SOURCE_DIR}/aasdk")
else()
    set(AASDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/aasdk" CACHE PATH
        "AASDK source/build root")
    if(NOT AASDK_LIBRARIES)
        set(AASDK_LIBRARIES "${AASDK_ROOT}/lib/libaasdk.a" CACHE FILEPATH
            "AASDK library")
    endif()
    if(NOT AASDK_PROTO_LIBRARIES)
        set(AASDK_PROTO_LIBRARIES "${AASDK_ROOT}/lib/libaasdk_proto.a" CACHE FILEPATH
            "AASDK protocol library")
    endif()
    if(NOT AASDK_INCLUDE_DIRS)
        set(AASDK_INCLUDE_DIRS "${AASDK_ROOT}/include" CACHE PATH
            "AASDK include directory")
    endif()
    if(NOT AASDK_PROTO_INCLUDE_DIRS)
        set(AASDK_PROTO_INCLUDE_DIRS "${AASDK_ROOT}" CACHE PATH
            "AASDK generated protocol include directory")
    endif()
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${base_directory}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${base_directory}/lib)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${base_directory}/bin)
set(EXECUTABLE_OUTPUT_PATH ${base_directory}/bin)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake_modules/")
SET(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_INIT} -Wall -pedantic -fPIC")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE
    "-g -O3 -DNDEBUG -flto -march=native -mtune=native -fomit-frame-pointer -fno-plt -ffunction-sections -fdata-sections"
    CACHE STRING "Release compiler flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto -Wl,--gc-sections"
    CACHE STRING "Release executable linker flags" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "-flto -Wl,--gc-sections"
    CACHE STRING "Release shared linker flags" FORCE)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(OPENAUTO_LOG_ERRORS_ONLY)
endif()

find_package(Boost REQUIRED COMPONENTS system log OPTIONAL_COMPONENTS unit_test_framework)
find_package(libusb-1.0 REQUIRED)
find_package(Qt6 COMPONENTS Multimedia MultimediaWidgets Bluetooth Quick OpenGL)
find_package(Qt6WaylandClient CONFIG REQUIRED)
find_package(Qt6MultimediaPrivate CONFIG REQUIRED)
find_package(EGL REQUIRED)
list(APPEND CMAKE_MODULE_PATH "${Qt6_DIR}")
find_package(VAAPI REQUIRED)
foreach(openauto_optional_x11_dependency IN ITEMS X11 Xrandr Xext)
    if(NOT TARGET ${openauto_optional_x11_dependency})
        add_library(${openauto_optional_x11_dependency} INTERFACE IMPORTED)
    endif()
endforeach()
find_library(OPENAUTO_X11_LIBRARY X11 REQUIRED)
find_library(OPENAUTO_XRANDR_LIBRARY Xrandr REQUIRED)
find_library(OPENAUTO_XEXT_LIBRARY Xext REQUIRED)
foreach(openauto_ffmpeg_component IN ITEMS avformat avcodec swresample swscale avutil)
    find_library(OPENAUTO_FFMPEG_${openauto_ffmpeg_component}_LIBRARY
        NAMES ${openauto_ffmpeg_component} REQUIRED)
    if(NOT TARGET FFmpeg::${openauto_ffmpeg_component})
        add_library(FFmpeg::${openauto_ffmpeg_component} UNKNOWN IMPORTED)
        set_target_properties(FFmpeg::${openauto_ffmpeg_component} PROPERTIES
            IMPORTED_LOCATION "${OPENAUTO_FFMPEG_${openauto_ffmpeg_component}_LIBRARY}")
    endif()
endforeach()
find_package(Qt6FFmpegMediaPluginImplPrivate CONFIG REQUIRED)
find_package(Protobuf REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(rtaudio REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale)

if(WIN32)
    set(WINSOCK2_LIBRARIES "ws2_32")
endif(WIN32)

if(RPI3_BUILD)
    add_definitions(-DUSE_OMX -DOMX_SKIP64BIT -DRASPBERRYPI3)
    set(BCM_HOST_LIBRARIES "/opt/vc/lib/libbcm_host.so")
    set(BCM_HOST_INCLUDE_DIRS "/opt/vc/include")
    set(ILCLIENT_INCLUDE_DIRS "/opt/vc/src/hello_pi/libs/ilclient")
    set(ILCLIENT_LIBRARIES "/opt/vc/src/hello_pi/libs/ilclient/libilclient.a;/opt/vc/lib/libvcos.so;/opt/vc/lib/libvcilcs.a;/opt/vc/lib/libvchiq_arm.so")
endif(RPI3_BUILD)

include_directories(${CMAKE_CURRENT_BINARY_DIR}
                    ${Qt6Multimedia_INCLUDE_DIRS}
                    ${Qt6MultimediaWidgets_INCLUDE_DIRS}
                    ${Qt6Widgets_INCLUDE_DIRS}
                    ${Qt6Bluetooth_INCLUDE_DIRS}
                    ${Boost_INCLUDE_DIRS}
                    ${LIBUSB_1_INCLUDE_DIRS}
                    ${PROTOBUF_INCLUDE_DIR}
                    ${OPENSSL_INCLUDE_DIR}
                    ${RTAUDIO_INCLUDE_DIRS}
                    ${AASDK_PROTO_INCLUDE_DIRS}
                    ${AASDK_INCLUDE_DIRS}
                    ${BCM_HOST_INCLUDE_DIRS}
                    ${ILCLIENT_INCLUDE_DIRS}
                    ${include_directory})
								
link_directories(${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

set(common_include_directory ${include_directory}/f1x/openauto/Common)

set(autoapp_sources_directory ${sources_directory}/autoapp)
set(autoapp_include_directory ${include_directory}/f1x/openauto/autoapp)
file(GLOB_RECURSE autoapp_source_files ${autoapp_sources_directory}/*.ui ${autoapp_sources_directory}/*.cpp ${autoapp_include_directory}/*.hpp ${common_include_directory}/*.hpp ${resources_directory}/*.qrc)

add_executable(autoapp ${autoapp_source_files})

target_link_libraries(autoapp
    ${AASDK_LIBRARIES}
    ${AASDK_PROTO_LIBRARIES}
    ${Boost_LIBRARIES}
    ${Qt6Multimedia_LIBRARIES}
    ${Qt6MultimediaWidgets_LIBRARIES}
    ${Qt6Bluetooth_LIBRARIES}
    Qt6::WaylandClient
    Qt6::FFmpegMediaPluginImplPrivate
    ${LIBUSB_1_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${BCM_HOST_LIBRARIES}
    ${ILCLIENT_LIBRARIES}
    ${WINSOCK2_LIBRARIES}
    ${RTAUDIO_LIBRARIES}
    PkgConfig::FFMPEG
    absl::log_internal_check_op
    ${OPENAUTO_X11_LIBRARY}
    ${OPENAUTO_XRANDR_LIBRARY}
    ${OPENAUTO_XEXT_LIBRARY}
)

set(btservice_sources_directory ${sources_directory}/btservice)
set(btservice_include_directory ${include_directory}/f1x/openauto/btservice)
file(GLOB_RECURSE btservice_source_files ${btservice_sources_directory}/*.cpp ${btservice_include_directory}/*.hpp ${common_include_directory}/*.hpp)

add_executable(btservice ${btservice_source_files})

target_link_libraries(btservice
                        ${AASDK_PROTO_LIBRARIES}
                        ${Boost_LIBRARIES}
                        ${Qt6Bluetooth_LIBRARIES}
                        ${Qt6MultimediaWidgets_LIBRARIES}
                        ${PROTOBUF_LIBRARIES}
                        )

target_include_directories(autoapp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/aasdk/)
target_include_directories(autoapp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/aasdk/include)
target_include_directories(autoapp PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/aasdk)
target_include_directories(btservice PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/aasdk)

# Unified entry point: 'cmake --build . --target build' compiles
# aasdk/aasdk_proto plus openauto's own executables. The default
# 'all' target already covers everything via target_link_libraries
# dependencies; 'clean' recurses into aasdk/ automatically.
if(OPENAUTO_USE_BUNDLED_AASDK)
    add_custom_target(build DEPENDS autoapp btservice aasdk aasdk_proto)
else()
    add_custom_target(build DEPENDS autoapp btservice)
endif()
