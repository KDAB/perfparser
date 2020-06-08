# taken from: https://github.com/facebook/folly/blob/master/CMake/FindZstd.cmake
# should be apache 2.0, cf.: https://github.com/facebook/folly/blob/master/LICENSE
#
# - Try to find Facebook zstd library
# This will define
# Zstd_FOUND
# Zstd_INCLUDE_DIR
# Zstd_LIBRARY
#

find_path(Zstd_INCLUDE_DIR NAMES zstd.h)
find_library(Zstd_LIBRARY NAMES zstd)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
    Zstd DEFAULT_MSG
    Zstd_LIBRARY Zstd_INCLUDE_DIR
)

mark_as_advanced(Zstd_INCLUDE_DIR Zstd_LIBRARY)
