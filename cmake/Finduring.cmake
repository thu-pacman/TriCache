# - Find uring
#
# URING_INCLUDE_DIR - Where to find liburing.h
# URING_LIBRARIES - List of libraries when using uring.
# URING_FOUND - True if uring found.

find_path(URING_INCLUDE_DIR 
    liburing.h 
    HINTS ${URING_ROOT}/include)

find_library(URING_LIBRARIES 
    liburing.a 
    uring 
    HINTS ${URING_ROOT}/lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(uring DEFAULT_MSG URING_LIBRARIES URING_INCLUDE_DIR)

mark_as_advanced(URING_INCLUDE_DIR URING_LIBRARIES)

