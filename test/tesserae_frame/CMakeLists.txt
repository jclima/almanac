add_executable(tesserae_frame_tests
  TesseraeFrameTest.cpp
  ${REPO_ROOT}/lib/Tesserae/TesseraeFrame.cpp
)

target_link_libraries(tesserae_frame_tests PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(tesserae_frame_tests)
