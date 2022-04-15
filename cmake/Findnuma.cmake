# Module for locating libnuma
#
# Read-only variables:
#   NUMA_FOUND
#     Indicates that the library has been found.
#
#   NUMA_INCLUDE_DIR
#     Points to the libnuma include directory.
#
#   NUMA_LIBRARY_DIRS
#     Points to the directory that contains the libraries.
#     The content of this variable can be passed to link_directories.
#
#   NUMA_LIBRARIES
#     Points to the libnuma that can be passed to target_link_libararies.
#
# Copyright (c) 2013-2020 MulticoreWare, Inc

include(FindPackageHandleStandardArgs)

find_path(NUMA_ROOT_DIR
  NAMES include/numa.h
  PATHS ENV NUMA_ROOT
  DOC "NUMA root directory")

find_path(NUMA_INCLUDE_DIR
  NAMES numa.h
  HINTS ${NUMA_ROOT_DIR}
  PATH_SUFFIXES include
  DOC "NUMA include directory")

find_library(NUMA_LIBRARIES
  NAMES numa
  HINTS ${NUMA_ROOT_DIR}
  DOC "NUMA library")

if (NUMA_LIBRARY)
    get_filename_component(NUMA_LIBRARY_DIRS ${NUMA_LIBRARY} PATH)
endif()

mark_as_advanced(NUMA_INCLUDE_DIR NUMA_LIBRARY_DIRS NUMA_LIBRARIES)

find_package_handle_standard_args(numa REQUIRED_VARS NUMA_ROOT_DIR NUMA_INCLUDE_DIR NUMA_LIBRARIES)
