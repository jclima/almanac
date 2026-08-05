add_executable(OpenSkyStatesParserTest
  OpenSkyStatesParserTest.cpp
  ${REPO_ROOT}/lib/JsonParser/OpenSkyStatesParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
  ${REPO_ROOT}/lib/Geo/GeoMath.cpp
)

target_include_directories(OpenSkyStatesParserTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Geo
)

target_link_libraries(OpenSkyStatesParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OpenSkyStatesParserTest)
