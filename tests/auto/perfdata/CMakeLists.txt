add_qtc_test(tst_perfdata
  DEPENDS Qt::Core Qt::Test perfparser_lib
  INCLUDES ../shared
  SOURCES
    ../shared/perfparsertestclient.cpp
    perfdata.qrc
    tst_perfdata.cpp
)
