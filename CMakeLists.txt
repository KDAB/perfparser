find_package(elfutils)

if (NOT elfutils_FOUND)
  message(STATUS "PerfParser is disabled. Set ELFUTILS_INSTALL_DIR to enable it.")
  return()
endif()

set(RUSTC_DEMANGLE_INCLUDE_DIR "" CACHE STRING "Path to the folder containing rustc_demangle.h from https://github.com/alexcrichton/rustc-demangle")
set(RUSTC_DEMANGLE_LIBRARY "" CACHE STRING "Path to the librustc_demangle.so library from https://github.com/alexcrichton/rustc-demangle")
option(WITH_RUSTC_DEMANGLE "Build rust demangle support" OFF)
add_feature_info("Rust demangler for perfparser" WITH_RUSTC_DEMANGLE "Requires RUSTC_DEMANGLE_INCLUDE_DIR and RUSTC_DEMANGLE_LIBRARY to be specified")

add_subdirectory(app)
add_subdirectory(tests)
