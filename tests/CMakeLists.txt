add_executable(ringdown_tests test_algorithms.cpp test_main.cpp test_version.cpp)
target_link_libraries(ringdown_tests PRIVATE RingDownAnalysisCpp::ringdownanalysis)
target_compile_definitions(ringdown_tests PRIVATE RINGDOWN_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
ringdown_apply_project_options(ringdown_tests)

add_test(NAME ringdown_tests COMMAND ringdown_tests)
