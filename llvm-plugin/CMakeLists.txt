cmake_minimum_required(VERSION 3.13.4)
project(tricache-instrument)

find_package(LLVM 13 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include_directories(${LLVM_INCLUDE_DIRS})

add_subdirectory(TriCacheAlloc)
add_subdirectory(TriCacheInstrument)

set(TRI_INST_OUTPUT "$<TARGET_FILE:TriCacheInstrument>")
set(TRI_ALLOC_OUTPUT "$<TARGET_FILE:TriCacheAlloc>")

add_custom_target(tri-inst-output DEPENDS "${TRI_INST_OUTPUT}")
add_dependencies(tri-inst-output TriCacheInstrument)
add_custom_target(tri-inst-alloc-output DEPENDS "${TRI_ALLOC_OUTPUT}")
add_dependencies(tri-inst-alloc-output TriCacheAlloc)

add_library(instrument INTERFACE)
add_dependencies(instrument tri-inst-output)
target_compile_options(instrument INTERFACE "-fpass-plugin=${TRI_INST_OUTPUT}")

add_library(instrument-alloc INTERFACE)
add_dependencies(instrument-alloc tri-inst-alloc-output)
target_link_libraries(instrument-alloc INTERFACE instrument)
target_compile_options(instrument-alloc INTERFACE "-fpass-plugin=${TRI_ALLOC_OUTPUT}")
