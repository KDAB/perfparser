add_qtc_library(perfparser_lib STATIC
  ALLOW_ASCII_CASTS
  PUBLIC_DEPENDS
    Qt5::Core
    Qt5::Network
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
)

if (Zstd_FOUND)
    target_include_directories(perfparser_lib PUBLIC ${Zstd_INCLUDE_DIR})
    target_link_libraries(perfparser_lib PUBLIC ${Zstd_LIBRARY})
    target_compile_definitions(perfparser_lib PUBLIC HAVE_ZSTD=1)
endif()

add_qtc_executable(perfparser
  DEPENDS
    perfparser_lib
  SOURCES
    main.cpp
)
