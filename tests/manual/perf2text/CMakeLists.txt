add_qtc_executable(perf2text
  DEFINES MANUAL_TEST
  DEPENDS perfparser_lib
  INCLUDES ../../auto/shared/
  SOURCES
    ../../auto/shared/perfparsertestclient.cpp
    perf2text.cpp
)
