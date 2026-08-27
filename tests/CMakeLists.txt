add_executable(dogdouclix_tests
    test_main.cpp
)

target_link_libraries(dogdouclix_tests PRIVATE
    dogdouclix_core
)

add_test(NAME dogdouclix_tests COMMAND dogdouclix_tests)