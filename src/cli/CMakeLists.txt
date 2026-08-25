add_executable(dogdouclix
    main.cpp
)

target_link_libraries(dogdouclix PRIVATE
    dogdouclix_core
)

set_target_properties(dogdouclix PROPERTIES
    OUTPUT_NAME "dogdouclix"
)