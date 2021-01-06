find_package(elfutils)

if (NOT elfutils_FOUND)
  message(STATUS "PerfParser is disabled. Set ELFUTILS_INSTALL_DIR to enable it.")
  return()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Zstd)

find_package(LibRustcDemangle)
set_package_properties(LibRustcDemangle PROPERTIES
        DESCRIPTION "Demangling for Rust symbols, written in Rust."
        PURPOSE "Demangling of Rust symbols"
        URL "https://github.com/alexcrichton/rustc-demangle"
        TYPE RUNTIME)

add_subdirectory(app)
add_subdirectory(tests)
