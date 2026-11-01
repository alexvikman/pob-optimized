add_executable(pob_host
    main.cpp)

set_target_properties(pob_host PROPERTIES OUTPUT_NAME "PathOfBuilding")

target_link_libraries(pob_host PRIVATE
    SDL3::SDL3
    luajit
    ${POB_ZLIB_TARGET}
    ${POB_CURL_TARGET}
    stb)

target_compile_definitions(pob_host PRIVATE
    POB_HOST_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(pob_host PRIVATE /W4 /utf-8)
else()
    target_compile_options(pob_host PRIVATE -Wall -Wextra)
endif()
