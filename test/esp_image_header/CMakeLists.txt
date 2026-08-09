add_executable(EspImageHeaderTest
  EspImageHeaderTest.cpp
  ${REPO_ROOT}/lib/EspImage/EspImageHeader.cpp
)

target_include_directories(EspImageHeaderTest PRIVATE
  ${REPO_ROOT}/lib/EspImage
)

target_link_libraries(EspImageHeaderTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(EspImageHeaderTest)
