#
# CMakeLists.txt for iota
#
cmake_minimum_required(VERSION 3.0.0)
project(iota VERSION 0.1.0)

set(CXX_VERSION_FLAGS "-std=c++20")
set(CXX_WARNING_FLAGS "-Werror -Wno-trigraphs -Wno-missing-field-initializers -Wno-missing-prototypes -Werror=return-type -Wdocumentation \
-Wunreachable-code -Wno-non-virtual-dtor -Wno-overloaded-virtual -Wno-exit-time-destructors -Wno-missing-braces -Wparentheses -Wswitch \
-Wunused-function -Wno-unused-label -Wno-unused-parameter -Wunused-variable -Wunused-value -Wempty-body -Wuninitialized \
-Wconditional-uninitialized -Wno-unknown-pragmas -Wno-shadow -Wno-four-char-constants -Wno-conversion -Wconstant-conversion \
-Wint-conversion -Wbool-conversion -Wenum-conversion -Wno-float-conversion -Wnon-literal-null-conversion \
-Wshorten-64-to-32 -Wno-newline-eof -Wno-c++11-extensions -Wdeprecated-declarations -Winvalid-offsetof -Wno-sign-conversion \
-Winfinite-recursion -Wmove -Wcomma -Wstrict-prototypes -Wrange-loop-analysis -Wunguarded-availability -Wno-nullability-completeness \
-fvisibility-inlines-hidden -fasm-blocks -fstrict-aliasing -fno-common")
set(CMAKE_CXX_FLAGS "${CXX_VERSION_FLAGS} ${CXX_ARCH_FLAGS} ${CXX_WARNING_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

IF(DEFINED ENV{UDIR})
set(PREFIX $ENV{UDIR})
set(CMAKE_INSTALL_PREFIX ${PREFIX})
ELSE()
set(PREFIX /usr/local)
set(CMAKE_INSTALL_PREFIX ${PREFIX})
ENDIF()

include(CTest)
enable_testing()

find_package(UU REQUIRED)

add_executable(ref ref-tool.cpp)
add_executable(match match-tool.cpp)
add_executable(search search-tool.cpp)

target_include_directories(ref PRIVATE "${PREFIX}/include")
target_include_directories(match PRIVATE "${PREFIX}/include")
target_include_directories(search PRIVATE "${PREFIX}/include")

target_link_libraries(ref UU)
target_link_libraries(match UU)
target_link_libraries(search UU)

install(TARGETS ref match search DESTINATION bin)
