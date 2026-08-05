add_executable(AircraftInfoParserTest
  AircraftInfoParserTest.cpp
  ${REPO_ROOT}/lib/JsonParser/AircraftInfoParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(AircraftInfoParserTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(AircraftInfoParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AircraftInfoParserTest)
