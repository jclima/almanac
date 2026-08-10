add_executable(Game2048LayoutTest
  Game2048LayoutTest.cpp
)

target_include_directories(Game2048LayoutTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(Game2048LayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(Game2048LayoutTest)
