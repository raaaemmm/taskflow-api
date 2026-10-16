add_executable(unit_tests validation_test.cc)
target_link_libraries(unit_tests PRIVATE project_options Drogon::Drogon)
add_test(NAME unit_tests COMMAND unit_tests)
