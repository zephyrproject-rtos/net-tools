cmake_minimum_required(VERSION 3.8)
project(virtual-hub)


set(HEADER_FILES src/graphs.h)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLib2 REQUIRED glib-2.0)

include_directories (${GLib2_INCLUDE_DIRS})
add_executable(hub src/hub.c src/graphs.c ${HEADER_FILES})
target_link_libraries (hub ${GLib2_LIBRARIES} pthread)

