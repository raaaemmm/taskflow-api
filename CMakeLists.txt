cmake_minimum_required(VERSION 3.20)
project(todo_api LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(ENABLE_SANITIZERS "Build with ASan + UBSan (GCC/Clang only)" OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# libsodium: vcpkg config package on Windows, pkg-config elsewhere
if(MSVC)
  find_package(unofficial-sodium CONFIG REQUIRED)
  set(SODIUM_TARGET unofficial-sodium::sodium)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
  set(SODIUM_TARGET PkgConfig::SODIUM)
endif()

include(FetchContent)
set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JWT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(jwt-cpp
  GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
  GIT_TAG v0.7.0)
FetchContent_MakeAvailable(jwt-cpp)

add_library(project_options INTERFACE)
target_include_directories(project_options INTERFACE ${CMAKE_SOURCE_DIR}/src)

if(MSVC)
  target_compile_options(project_options INTERFACE /W4 /permissive- /utf-8 /bigobj /Zc:__cplusplus)
  target_compile_definitions(project_options INTERFACE
    _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  target_compile_options(project_options INTERFACE
    -Wall -Wextra -Wpedantic -fstack-protector-strong -D_GLIBCXX_ASSERTIONS)
  target_link_options(project_options INTERFACE -Wl,-z,relro,-z,now)
  if(ENABLE_SANITIZERS)
    target_compile_options(project_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(project_options INTERFACE -fsanitize=address,undefined)
  endif()
endif()

add_executable(todo_api src/main.cc)
target_link_libraries(todo_api PRIVATE project_options Drogon::Drogon
  ${SODIUM_TARGET} jwt-cpp::jwt-cpp OpenSSL::SSL OpenSSL::Crypto)

enable_testing()
add_subdirectory(tests)