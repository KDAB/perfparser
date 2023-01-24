add_qtc_library(perfparser_lib STATIC
  ALLOW_ASCII_CASTS
  PUBLIC_DEPENDS
    Qt::Core
    Qt::Network
    elfutils::dw
    elfutils::elf
  PUBLIC_INCLUDES ./
  SOURCES
    perfaddresscache.cpp
    perfattributes.cpp perfattributes.h
    perfheader.cpp perfheader.h
    perffilesection.cpp perffilesection.h
    perffeatures.cpp perffeatures.h
    perfdata.cpp perfdata.h
    perfunwind.cpp perfunwind.h
    perfregisterinfo.cpp perfregisterinfo.h
    perfstdin.cpp perfstdin.h
    perfsymboltable.cpp perfsymboltable.h
    perfelfmap.cpp perfelfmap.h
    perfkallsyms.cpp perfkallsyms.h
    perftracingdata.cpp perftracingdata.h
    perfdwarfdiecache.cpp perfdwarfdiecache.h
    perfeucompat.h
    demangler.cpp demangler.h
)

if (Zstd_FOUND)
    target_include_directories(perfparser_lib PUBLIC ${Zstd_INCLUDE_DIR})
    target_link_libraries(perfparser_lib PUBLIC ${Zstd_LIBRARY})
    target_compile_definitions(perfparser_lib PUBLIC HAVE_ZSTD=1)
endif()

if (HAVE_DWFL_GET_DEBUGINFOD_CLIENT)
    target_link_libraries(perfparser_lib PRIVATE elfutils::debuginfod)
    target_compile_definitions(perfparser_lib PRIVATE HAVE_DWFL_GET_DEBUGINFOD_CLIENT=1)
endif()

add_qtc_executable(perfparser
  DEPENDS
    perfparser_lib
  SOURCES
    main.cpp
)
