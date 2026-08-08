add_executable(SemVerTest
  SemVerTest.cpp
  ${REPO_ROOT}/lib/Version/SemVer.cpp
)

target_include_directories(SemVerTest PRIVATE
  ${REPO_ROOT}/lib/Version
)

target_link_libraries(SemVerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SemVerTest)
