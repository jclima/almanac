add_executable(ZipGeocodeParserTest
  ZipGeocodeParserTest.cpp
  ${REPO_ROOT}/lib/JsonParser/ZipGeocodeParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(ZipGeocodeParserTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(ZipGeocodeParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ZipGeocodeParserTest)
