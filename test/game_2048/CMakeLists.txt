add_executable(Game2048Test
  Game2048Test.cpp
  ${REPO_ROOT}/lib/Game2048/Game2048.cpp
)

target_include_directories(Game2048Test PRIVATE
  ${REPO_ROOT}/lib/Game2048
)

target_link_libraries(Game2048Test PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(Game2048Test)
