# LuaJIT has no CMake build; drive its own makefile (or msvcbuild.bat on Windows)
# and expose the resulting static library as the imported target `luajit`.

include(ExternalProject)

set(LUAJIT_COMMIT ea9eb7edb5eac4a9d175d74523db34fac0fa5180) # v2.1 branch
set(LUAJIT_PREFIX "${CMAKE_BINARY_DIR}/_deps/luajit")
set(LUAJIT_SRC "${LUAJIT_PREFIX}/src/luajit_ext")

if(MSVC)
    # msvcbuild.bat must run from src/ inside a Visual Studio developer environment.
    set(LUAJIT_LIB "${LUAJIT_SRC}/src/lua51.lib")
    set(LUAJIT_BUILD ${CMAKE_COMMAND} -E chdir "${LUAJIT_SRC}/src" cmd /c msvcbuild.bat static)
else()
    set(LUAJIT_LIB "${LUAJIT_SRC}/src/libluajit.a")
    set(LUAJIT_MAKE_ARGS BUILDMODE=static)
    if(APPLE)
        list(APPEND LUAJIT_MAKE_ARGS MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
    endif()
    set(LUAJIT_BUILD make -C src -j8 ${LUAJIT_MAKE_ARGS} libluajit.a)
endif()

ExternalProject_Add(luajit_ext
    PREFIX            "${LUAJIT_PREFIX}"
    GIT_REPOSITORY    https://github.com/LuaJIT/LuaJIT.git
    GIT_TAG           ${LUAJIT_COMMIT}
    UPDATE_DISCONNECTED TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ${LUAJIT_BUILD}
    BUILD_IN_SOURCE   TRUE
    INSTALL_COMMAND   ""
    BUILD_BYPRODUCTS  "${LUAJIT_LIB}")

# The include directory must exist at configure time for the imported target.
file(MAKE_DIRECTORY "${LUAJIT_SRC}/src")

add_library(luajit STATIC IMPORTED GLOBAL)
set_target_properties(luajit PROPERTIES
    IMPORTED_LOCATION "${LUAJIT_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${LUAJIT_SRC}/src")
add_dependencies(luajit luajit_ext)
