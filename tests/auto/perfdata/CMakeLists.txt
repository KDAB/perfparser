add_qtc_test(tst_perfdata
  DEPENDS Qt5::Core Qt5::Test perfparser_lib
  INCLUDES ../shared
  SOURCES
    ../shared/perfparsertestclient.cpp
    perfdata.qrc
    tst_perfdata.cpp
)
