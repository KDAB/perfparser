find_package(elfutils)

if (NOT elfutils_FOUND)
  message(STATUS "PerfParser is disabled. Set ELFUTILS_INSTALL_DIR to enable it.")
  return()
endif()

find_package(Zstd)

add_subdirectory(app)
add_subdirectory(tests)
