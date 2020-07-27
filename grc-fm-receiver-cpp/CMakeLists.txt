################################################################################
# CMakeLists.txt
#
# Copyright 2014-2015 Ettus Research LLC
# Copyright 2018 Ettus Research, a National Instruments Company
#
# SPDX-License-Identifier: GPL-3.0
#
################################################################################

cmake_minimum_required(VERSION 2.8)

project(fm_receiver CXX)

################################################################################
# Set up Boost and UHD
################################################################################

# Choose a static or shared-library build (shared is default, and static will
# probably need some special care!)
# Set this to ON in order to link a static build of UHD:
option(UHD_USE_STATIC_LIBS OFF)

# This example also requires Boost:
set(BOOST_REQUIRED_COMPONENTS
    program_options
    system
    thread
#    chrono
#    date_time
#    filesystem
#    regex
#    unit_test_framework
#    serialization
)

find_package(Boost "1.46" REQUIRED ${BOOST_REQUIRED_COMPONENTS})
find_package(UHD "3.13.0" REQUIRED)
find_package(Gnuradio "3.7.0" REQUIRED)

################################################################################
# Link libraries
################################################################################
include_directories(
    ${Boost_INCLUDE_DIRS}
    ${GR_AUDIO_INCLUDE_DIRS}
    ${GR_ANALOG_INCLUDE_DIRS}
    ${GNURADIO_RUNTIME_INCLUDE_DIRS}
    ${UHD_INCLUDE_DIRS}
)
link_directories(${Boost_LIBRARY_DIRS})

################################################################################
# Configure Compiler
################################################################################

# Full C++11 came with GCC 4.7, Clang >= 3.3.0, Apple Clang >= 500
set(GCC_MIN_VERSION "4.8.0")
set(CLANG_MIN_VERSION "3.3.0")
set(APPLECLANG_MIN_VERSION "500")

################################################################################
# Enable generation of compile_commands.json for code completion engines
################################################################################

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

################################################################################
# Make the executable
################################################################################

add_executable(fm_radio source/fm_radio.cpp)
target_link_libraries(fm_radio
  gnuradio-analog
  gnuradio-audio
  gnuradio-filter
  gnuradio-runtime
  gnuradio-uhd)


set(GNURADIO_LIBS "-lgnuradio-runtime -lgnuradio-blocks")
add_definitions(${GNURADIO_LIBS})

# Shared library case: All we need to do is link against the library, and
# anything else we need (in this case, some Boost libraries):
if(NOT UHD_USE_STATIC_LIBS)
    message(STATUS "Linking against shared UHD library.")
    target_link_libraries(fm_radio ${UHD_LIBRARIES} ${Boost_LIBRARIES})
# Shared library case: All we need to do is link against the library, and
# anything else we need (in this case, some Boost libraries):
else(NOT UHD_USE_STATIC_LIBS)
    message(STATUS "Linking against static UHD library.")
    target_link_libraries(fm_radio
        # We could use ${UHD_LIBRARIES}, but linking requires some extra flags,
        # so we use this convenience variable provided to us
        ${UHD_STATIC_LIB_LINK_FLAG}
        # Also, when linking statically, we need to pull in all the deps for
        # UHD as well, because the dependencies don't get resolved automatically
        ${UHD_STATIC_LIB_DEPS}
    )
endif(NOT UHD_USE_STATIC_LIBS)

################################################################################
