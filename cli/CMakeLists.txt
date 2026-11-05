add_executable(hfstudio main.cpp)
target_link_libraries(hfstudio PRIVATE hfstudio_core)
target_compile_options(hfstudio PRIVATE /utf-8 /W4 /permissive-)
target_compile_definitions(hfstudio PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
