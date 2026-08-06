add_executable(HomeMenuLayoutTest
  HomeMenuLayoutTest.cpp
)

target_include_directories(HomeMenuLayoutTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(HomeMenuLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HomeMenuLayoutTest)
