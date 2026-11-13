# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
#
# The applications as one project for an IDE that reads CMake: the code of
# each is understood, and building one, running it on a device and the live
# page are targets (<name>, <name>-run, <name>-serve). Needs the perinkpheral
# repository for its SDK and tools (PERINKPHERAL, by default a checkout
# beside this one) and wasi-sdk (WASI_SDK_PATH). build.sh is still what
# makes the catalog; the target `catalog` runs it.
cmake_minimum_required(VERSION 3.20)

if(DEFINED ENV{PERINKPHERAL})
    set(PERINKPHERAL "$ENV{PERINKPHERAL}")
else()
    set(PERINKPHERAL "${CMAKE_CURRENT_LIST_DIR}/../perinkpheral")
endif()
if(NOT EXISTS "${PERINKPHERAL}/sdk/cmake/perinkpheral.cmake")
    message(FATAL_ERROR "No perinkpheral checkout at ${PERINKPHERAL}: set PERINKPHERAL to where it is.")
endif()
include("${PERINKPHERAL}/sdk/cmake/perinkpheral.cmake")
project(perinkpheral-apps C)

file(GLOB manifests CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/apps/*/manifest.json")
foreach(manifest ${manifests})
    get_filename_component(app_dir "${manifest}" DIRECTORY)
    perinkpheral_app("${app_dir}")
endforeach()

add_custom_target(catalog
                  COMMAND "${CMAKE_COMMAND}" -E env "WASI_SDK_PATH=$ENV{WASI_SDK_PATH}"
                          "PERINKPHERAL=${PERINKPHERAL}" sh "${CMAKE_CURRENT_LIST_DIR}/build.sh"
                  USES_TERMINAL VERBATIM)
