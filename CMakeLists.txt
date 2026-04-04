cmake_minimum_required(VERSION 3.16)
project(spa-aec-speex LANGUAGES C)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)

pkg_check_modules(SPEEXDSP REQUIRED speexdsp)
pkg_check_modules(LIBPIPEWIRE REQUIRED libpipewire-0.3)

add_library(spa-aec-speex MODULE aec-speex.c)

set_target_properties(spa-aec-speex PROPERTIES
    PREFIX "lib"
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)

target_include_directories(spa-aec-speex PRIVATE
    ${SPEEXDSP_INCLUDE_DIRS}
    ${LIBPIPEWIRE_INCLUDE_DIRS}
)

target_compile_options(spa-aec-speex PRIVATE
    ${SPEEXDSP_CFLAGS_OTHER}
    ${LIBPIPEWIRE_CFLAGS_OTHER}
)

target_link_libraries(spa-aec-speex PRIVATE
    ${SPEEXDSP_LIBRARIES}
)

install(TARGETS spa-aec-speex
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/spa-0.2/aec
)
