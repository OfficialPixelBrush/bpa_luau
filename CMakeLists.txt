cmake_minimum_required(VERSION 3.16)
project(bpa_luau LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Luau is built from source and linked statically into the addon, so there is
# no runtime dependency on it. To use a local checkout instead of downloading:
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_LUAU=/path/to/luau
include(FetchContent)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    # Only build the Luau targets we actually link (VM, Compiler, Ast, ...).
    FetchContent_Declare(
        luau
        GIT_REPOSITORY https://github.com/luau-lang/luau.git
        GIT_TAG        0.743
        GIT_SHALLOW    TRUE
        EXCLUDE_FROM_ALL
    )
else()
    FetchContent_Declare(
        luau
        GIT_REPOSITORY https://github.com/luau-lang/luau.git
        GIT_TAG        0.743
        GIT_SHALLOW    TRUE
    )
endif()
set(LUAU_BUILD_CLI   OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB   OFF CACHE BOOL "" FORCE)
set(LUAU_WERROR      OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(luau)

add_library(bpa_luau MODULE src/bpa_luau.cpp)
target_link_libraries(bpa_luau PRIVATE Luau.VM Luau.Compiler)
target_compile_options(bpa_luau PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fvisibility=hidden>)
# bp_addon() is exported explicitly in the source.
set_target_properties(bpa_luau PROPERTIES PREFIX "")
