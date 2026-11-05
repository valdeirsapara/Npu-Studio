add_library(hfstudio_core STATIC
    src/http.cpp
    src/hub_client.cpp
    src/model_manager.cpp
    src/text_engine.cpp
)
target_include_directories(hfstudio_core
    PUBLIC include
    PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_link_libraries(hfstudio_core PUBLIC openvino::genai PRIVATE winhttp bcrypt)
target_compile_options(hfstudio_core PRIVATE /utf-8 /W4 /permissive-)
target_compile_definitions(hfstudio_core PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
