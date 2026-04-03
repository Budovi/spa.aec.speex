cmake_minimum_required(VERSION 3.16)
project(spa-aec-speex LANGUAGES CXX)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)

pkg_check_modules(SPEEXDSP REQUIRED speexdsp)
pkg_check_modules(LIBPIPEWIRE REQUIRED libpipewire-0.3)

# aec-speex.c uses C++ syntax (variable declaration in if-condition)
# and must be compiled as C++17.
add_library(spa-aec-speex MODULE aec-speex.c)
set_source_files_properties(aec-speex.c PROPERTIES LANGUAGE CXX)

set_target_properties(spa-aec-speex PROPERTIES
    PREFIX "lib"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
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
