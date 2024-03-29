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

find_package(LibDDemangle)
set_package_properties(LibDDemangle PROPERTIES
        DESCRIPTION "Demangling for D symbols, written in D."
        PURPOSE "Demangling of D symbols"
        URL "https://github.com/lievenhey/d_demangler"
        TYPE RUNTIME)


add_definitions(-DQT_NO_CAST_FROM_ASCII
                -DQT_NO_CAST_TO_ASCII
                -DQT_USE_QSTRINGBUILDER
                -DQT_NO_FOREACH)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wcast-align -Wchar-subscripts -Wformat-security -Wno-long-long -Wpointer-arith -Wnon-virtual-dtor -Woverloaded-virtual")
    # Make some warnings errors
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type -Werror=init-self -Werror=undef")
endif()

add_subdirectory(app)
add_subdirectory(tests)
