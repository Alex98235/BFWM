project(clay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CLAY_SRC "${CMAKE_CURRENT_SOURCE_DIR}/clay_impl.cpp")

add_library(clay STATIC ${CLAY_SRC})

target_compile_options(clay PRIVATE -w)

target_include_directories(clay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
