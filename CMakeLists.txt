cmake_minimum_required(VERSION 3.1.0)

project(wpe-testbed VERSION 0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_SOURCE_DIR}/cmake")

find_package(PkgConfig REQUIRED)

pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(GBM REQUIRED gbm)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLES REQUIRED glesv2)
pkg_check_modules(WLCLIENT REQUIRED wayland-client)
pkg_check_modules(WLSERVER REQUIRED wayland-server)

include(FindWaylandProtocols)
set(WAYLAND_PROTOCOLS_DEST_DIR "WaylandProtocols.dir")

message("Set wayland-scanner set to: ${WAYLAND_SCANNER}")
message("Set wayland protocols dir: ${WAYLAND_PROTOCOLS_DIR}")

function(run_wayland_scanner _protocol _dest_dir)
    get_filename_component(proto_name ${_protocol} NAME_WE)
    execute_process(
        COMMAND mkdir -p ${_dest_dir}
        COMMAND "${WAYLAND_SCANNER}" private-code "${WAYLAND_PROTOCOLS_DIR}${protocol}.xml" "${_dest_dir}/${proto_name}-protocol.c"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        ERROR_QUIET)
    execute_process(
        COMMAND mkdir -p ${WAYLAND_PROTOCOLS_DEST_DIR}
        COMMAND "${WAYLAND_SCANNER}" client-header "${WAYLAND_PROTOCOLS_DIR}${protocol}.xml" "${_dest_dir}/${proto_name}-client-protocol.h"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        ERROR_QUIET)
endfunction()

foreach (protocol stable/xdg-shell/xdg-shell unstable/linux-dmabuf/linux-dmabuf-unstable-v1 unstable/linux-explicit-synchronization/linux-explicit-synchronization-unstable-v1)
    message("Generating Wayland source code for ${protocol}")
    run_wayland_scanner("${protocol}" "${CMAKE_BINARY_DIR}/${WAYLAND_PROTOCOLS_DEST_DIR}")
endforeach ()

include(CheckSymbolExists)
set(CMAKE_REQUIRED_LIBRARIES ${GBM_LIBRARIES})
set(CMAKE_REQUIRED_INCLUDES ${GBM_INCLUDE_DIRS})
check_symbol_exists(gbm_bo_create_with_modifiers2 "gbm.h" HAVE_GBM_BO_CREATE_WITH_MODIFIERS2)
check_symbol_exists(gbm_bo_get_fd_for_plane "gbm.h" HAVE_GBM_BO_GET_FD_FOR_PLANE)

add_executable(wpe-testbed-wayland
    Application.cpp
    DMABuffer.cpp
    DRM.cpp
    EGL.cpp
    GBM.cpp
    Statistics.cpp
    Tile.cpp
    TileRenderer.cpp
    Utilities.cpp
    Wayland.cpp
    WaylandWindow.cpp
    main-wayland.cpp
    ${CMAKE_BINARY_DIR}/${WAYLAND_PROTOCOLS_DEST_DIR}/xdg-shell-protocol.c
    ${CMAKE_BINARY_DIR}/${WAYLAND_PROTOCOLS_DEST_DIR}/linux-dmabuf-unstable-v1-protocol.c
    ${CMAKE_BINARY_DIR}/${WAYLAND_PROTOCOLS_DEST_DIR}/linux-explicit-synchronization-unstable-v1-protocol.c
)

if (HAVE_GBM_BO_CREATE_WITH_MODIFIERS2)
    target_compile_definitions(wpe-testbed-wayland PRIVATE -DHAVE_GBM_BO_CREATE_WITH_MODIFIERS2)
endif ()

if (HAVE_GBM_BO_GET_FD_FOR_PLANE)
    target_compile_definitions(wpe-testbed-wayland PRIVATE -DHAVE_GBM_BO_GET_FD_FOR_PLANE)
endif ()

target_link_libraries(wpe-testbed-wayland
    m
    ${DRM_LIBRARIES}
    ${GBM_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLES_LIBRARIES}
    ${WLCLIENT_LIBRARIES}
)

target_include_directories(wpe-testbed-wayland PUBLIC
    ${DRM_INCLUDE_DIRS}
    ${GBM_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLES_INCLUDE_DIRS}
    ${WLCLIENT_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}/${WAYLAND_PROTOCOLS_DEST_DIR}/
)

link_directories(wpe-testbed-wayland
    ${DRM_LIBRARY_DIRS}
    ${GBM_LIBRARY_DIRS}
    ${EGL_LIBRARY_DIRS}
    ${GLES_LIBRARY_DIRS}
    ${WLCLIENT_LIBRARY_DIRS}
)

install(TARGETS wpe-testbed-wayland DESTINATION bin)
