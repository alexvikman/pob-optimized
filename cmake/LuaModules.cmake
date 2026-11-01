# Native Lua modules that PoB `require`s. They are built from the same upstream
# revisions SimpleGraphic pins, compiled as static libraries and registered in
# package.preload by the host (see docs/API.md §6).

include(FetchContent)

FetchContent_Declare(lua_curl
    GIT_REPOSITORY https://github.com/Lua-cURL/Lua-cURLv3.git
    GIT_TAG        9f8b6dba8b5ef1b26309a571ae75cda4034279e5
    SOURCE_SUBDIR  _no_cmake)
FetchContent_Declare(luautf8
    GIT_REPOSITORY https://github.com/starwing/luautf8.git
    GIT_TAG        d65ebfa4a00d6e582970ab729013bb2a629e74c9
    SOURCE_SUBDIR  _no_cmake)
# SOURCE_SUBDIR points nowhere so only the sources are fetched; we build them here.
FetchContent_MakeAvailable(lua_curl luautf8)

# --- lcurl (Lua-cURLv3) -------------------------------------------------------
file(GLOB LCURL_SOURCES "${lua_curl_SOURCE_DIR}/src/*.c")
add_library(lua_lcurl STATIC ${LCURL_SOURCES})
target_include_directories(lua_lcurl PRIVATE "${lua_curl_SOURCE_DIR}/src")
target_link_libraries(lua_lcurl PUBLIC luajit ${POB_CURL_TARGET})
# Its Lua 5.1 compat shim defines luaL_setfuncs, which LuaJIT 2.1 already has;
# rename lcurl's private copy so the static link has no duplicate symbol.
target_compile_definitions(lua_lcurl PRIVATE luaL_setfuncs=lcurl_compat_setfuncs)
if(NOT MSVC)
    target_compile_options(lua_lcurl PRIVATE -w)
endif()

# --- lua-utf8 (luautf8) -------------------------------------------------------
add_library(lua_utf8 STATIC "${luautf8_SOURCE_DIR}/lutf8lib.c")
target_include_directories(lua_utf8 PRIVATE "${luautf8_SOURCE_DIR}")
target_link_libraries(lua_utf8 PUBLIC luajit)

# --- minizip (from the zlib sources) for our lzip implementation --------------
set(MINIZIP_DIR "${zlib_SOURCE_DIR}/contrib/minizip")
add_library(minizip STATIC "${MINIZIP_DIR}/unzip.c" "${MINIZIP_DIR}/ioapi.c")
target_include_directories(minizip PUBLIC "${MINIZIP_DIR}")
target_link_libraries(minizip PUBLIC ${POB_ZLIB_TARGET})
target_compile_definitions(minizip PRIVATE NOCRYPT NOUNCRYPT)
if(APPLE)
    # fopen64 and friends do not exist on Darwin; plain stdio is 64-bit already.
    target_compile_definitions(minizip PRIVATE USE_FILE32API)
endif()
if(NOT MSVC)
    target_compile_options(minizip PRIVATE -w)
endif()
