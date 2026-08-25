add_library(dogdouclix_core STATIC
    common.cpp
    context_transform.cpp
    process_launcher.cpp
    forwarder.cpp
    config_parser.cpp
    target_resolver.cpp
)

target_include_directories(dogdouclix_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(dogdouclix_core PUBLIC
    userenv.lib
    advapi32.lib
    wtsapi32.lib
    kernel32.lib
)