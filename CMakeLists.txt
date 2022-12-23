#
# CMakeLists.txt for iota
#
cmake_minimum_required(VERSION 3.0.0)
project(iota VERSION 0.1.0)

set(CMAKE_CXX_FLAGS -std=c++17)
set(CMAKE_INSTALL_PREFIX /u)

include(CTest)
enable_testing()

add_executable(ref ref-tool.cpp)
add_executable(umatch match-tool.cpp)

find_package(UU REQUIRED)

target_include_directories(ref PRIVATE /u/include)
target_include_directories(umatch PRIVATE /u/include)

target_link_libraries(ref UU)
target_link_libraries(umatch UU)

install(TARGETS ref DESTINATION bin)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
