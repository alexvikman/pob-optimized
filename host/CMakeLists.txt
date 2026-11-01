add_executable(pob_host
    main.cpp
    app/App.cpp
    app/BootCheck.cpp
    app/Context.cpp
    app/Input.cpp
    core/Install.cpp
    core/Log.cpp
    core/Paths.cpp
    lua/ApiCore.cpp
    lua/ApiFiles.cpp
    lua/ApiRender.cpp
    lua/LuaUtil.cpp
    lua/Modules.cpp
    lua/SafetyNet.cpp
    lua/ScriptHost.cpp
    lua/SubScript.cpp
    render/DrawList.cpp
    render/Font.cpp
    render/Renderer.cpp
    render/StbImage.cpp
    render/Text.cpp
    render/Textures.cpp)

set_target_properties(pob_host PROPERTIES OUTPUT_NAME "PathOfBuilding")

target_include_directories(pob_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# libwebp only exports its source root; the public headers are under src/webp/.
target_include_directories(pob_host SYSTEM PRIVATE "${libwebp_SOURCE_DIR}/src")

target_link_libraries(pob_host PRIVATE
    SDL3::SDL3
    luajit
    lua_lcurl
    lua_utf8
    minizip
    webpdecoder
    ${POB_ZLIB_TARGET}
    ${POB_CURL_TARGET}
    stb)

target_compile_definitions(pob_host PRIVATE
    POB_HOST_VERSION="${PROJECT_VERSION}"
    # Development fallback when no bundled copy sits next to the executable.
    POB_DEV_BUNDLE_DIR="${PROJECT_SOURCE_DIR}/PathOfBuilding")

if(MSVC)
    target_compile_options(pob_host PRIVATE /W4 /utf-8)
else()
    target_compile_options(pob_host PRIVATE -Wall -Wextra)
endif()
