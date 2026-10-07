# Build Lua 5.4.7 as a static library from vendored source
# This removes the need for system-installed Lua packages
project(external LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LUA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lua-5.4.7/src")

# Core Lua source files (exclude lua.c interpreter and luac.c compiler)
set(LUA_CORE
    ${LUA_SRC_DIR}/lapi.c
    ${LUA_SRC_DIR}/lauxlib.c
    ${LUA_SRC_DIR}/lbaselib.c
    ${LUA_SRC_DIR}/lcode.c
    ${LUA_SRC_DIR}/lcorolib.c
    ${LUA_SRC_DIR}/lctype.c
    ${LUA_SRC_DIR}/ldblib.c
    ${LUA_SRC_DIR}/ldebug.c
    ${LUA_SRC_DIR}/ldo.c
    ${LUA_SRC_DIR}/ldump.c
    ${LUA_SRC_DIR}/lfunc.c
    ${LUA_SRC_DIR}/lgc.c
    ${LUA_SRC_DIR}/linit.c
    ${LUA_SRC_DIR}/liolib.c
    ${LUA_SRC_DIR}/llex.c
    ${LUA_SRC_DIR}/lmathlib.c
    ${LUA_SRC_DIR}/lmem.c
    ${LUA_SRC_DIR}/loadlib.c
    ${LUA_SRC_DIR}/lobject.c
    ${LUA_SRC_DIR}/lopcodes.c
    ${LUA_SRC_DIR}/loslib.c
    ${LUA_SRC_DIR}/lparser.c
    ${LUA_SRC_DIR}/lstate.c
    ${LUA_SRC_DIR}/lstring.c
    ${LUA_SRC_DIR}/lstrlib.c
    ${LUA_SRC_DIR}/ltable.c
    ${LUA_SRC_DIR}/ltablib.c
    ${LUA_SRC_DIR}/ltm.c
    ${LUA_SRC_DIR}/lundump.c
    ${LUA_SRC_DIR}/lutf8lib.c
    ${LUA_SRC_DIR}/lvm.c
    ${LUA_SRC_DIR}/lzio.c
)

add_library(lua STATIC ${LUA_CORE})

# Suppress warnings for vendored Lua code (we don't control it)
target_compile_options(lua PRIVATE -w)

# Include the Lua src directory so lua.h, lauxlib.h, lualib.h are found
target_include_directories(lua PUBLIC ${LUA_SRC_DIR})

set(TINY_REGEX_C_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tiny-regex-c")

set(TINY_REGEX_C_SOURCES ${TINY_REGEX_C_SRC_DIR}/re.c)

add_library(tiny-regex-c STATIC ${TINY_REGEX_C_SOURCES})

target_compile_options(tiny-regex-c PRIVATE -w)

target_include_directories(tiny-regex-c PUBLIC ${TINY_REGEX_C_SRC_DIR})

add_subdirectory(clay)
