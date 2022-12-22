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

find_package(UU REQUIRED)
# Add the include directories for the library
target_include_directories(ref PRIVATE /u/include)
# Link the library to your target
target_link_libraries(ref UU)

install(TARGETS ref DESTINATION bin)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
