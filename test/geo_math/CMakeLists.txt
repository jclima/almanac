add_executable(GeoMathTest
  GeoMathTest.cpp
  ${REPO_ROOT}/lib/Geo/GeoMath.cpp
)

target_include_directories(GeoMathTest PRIVATE
  ${REPO_ROOT}/lib/Geo
)

target_link_libraries(GeoMathTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GeoMathTest)
