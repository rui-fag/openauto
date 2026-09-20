cmake_minimum_required(VERSION 4.0.0)
project(aasdk CXX)

option(AASDK_BUILD_SHARED "Build AASDK as a shared library" OFF)

set(base_directory ${CMAKE_CURRENT_SOURCE_DIR})
set(sources_directory ${base_directory}/src)

set(include_directory ${base_directory}/include)
set(include_ut_directory ${base_directory}/include_ut)

if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${base_directory}/lib)
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${base_directory}/lib)
endif()

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${base_directory}/bin)
endif()
if(NOT EXECUTABLE_OUTPUT_PATH)
    set(EXECUTABLE_OUTPUT_PATH ${base_directory}/bin)
endif()

SET(CMAKE_CXX_STANDARD 17)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake_modules/")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_INIT} -fPIC -Wall -pedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE
    "-g -O3 -DNDEBUG -flto -march=native -mtune=native -fomit-frame-pointer -fno-plt -ffunction-sections -fdata-sections"
    CACHE STRING "Release compiler flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto -Wl,--gc-sections"
    CACHE STRING "Release executable linker flags" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "-flto -Wl,--gc-sections"
    CACHE STRING "Release shared linker flags" FORCE)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(AASDK_LOG_ERRORS_ONLY)
endif()

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

if(WIN32)
    set(WINSOCK2_LIBRARIES "ws2_32")
endif(WIN32)

if(AASDK_TEST)
    include(ExternalGtest)
endif(AASDK_TEST)

add_subdirectory(aasdk_proto)

find_package(Boost REQUIRED COMPONENTS system log OPTIONAL_COMPONENTS unit_test_framework)
find_package(libusb-1.0 REQUIRED)
find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)

set(AASDK_PROTO_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR})

include_directories(${AASDK_PROTO_INCLUDE_DIRS}
                        ${Boost_INCLUDE_DIRS}
                        ${LIBUSB_1_INCLUDE_DIRS}
                        ${PROTOBUF_INCLUDE_DIR}
                        ${OPENSSL_INCLUDE_DIR}
                        ${GTEST_INCLUDE_DIRS}
                        ${GMOCK_INCLUDE_DIRS}
                        ${include_directory}
                        ${include_ut_directory})

file(GLOB_RECURSE source_files ${sources_directory}/*.cpp)
file(GLOB_RECURSE include_files ${include_directory}/*.hpp)
file(GLOB_RECURSE tests_source_files ${sources_directory}/*.ut.cpp)
file(GLOB_RECURSE tests_include_files ${include_ut_directory}/*.hpp)

list(REMOVE_ITEM source_files ${tests_source_files})

if(AASDK_BUILD_SHARED)
    add_library(aasdk SHARED ${source_files} ${include_files})
else()
    add_library(aasdk STATIC ${source_files} ${include_files})
endif()

add_dependencies(aasdk aasdk_proto)
target_link_libraries(aasdk
                        aasdk_proto
                        ${Boost_LIBRARIES}
                        ${LIBUSB_1_LIBRARIES}
                        ${PROTOBUF_LIBRARIES}
                        ${OPENSSL_LIBRARIES}
                        ${WINSOCK2_LIBRARIES})

if(AASDK_TEST)
    add_executable(aasdk_ut
                    ${tests_source_files}
                    ${tests_include_files})

    add_dependencies(aasdk_ut aasdk)
    target_link_libraries(aasdk_ut
                            aasdk
                            ${GMOCK_LIBRARY_PATH}
                            ${GTEST_LIBRARY_PATH})

    if(AASDK_CODE_COVERAGE)
        include(CodeCoverage)
        append_coverage_compiler_flags()
        setup_target_for_coverage(NAME aasdk_coverage EXECUTABLE aasdk_ut DEPENDENCIES aasdk_ut)
    endif(AASDK_CODE_COVERAGE)
endif(AASDK_TEST)
