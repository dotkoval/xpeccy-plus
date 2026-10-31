cmake_minimum_required(VERSION 3.13...4.1)
project(sysnext-unpack-tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QTVERSION 6 CACHE STRING "Qt major version")
find_package(Qt${QTVERSION} REQUIRED COMPONENTS Core)
find_package(ZLIB REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# the 7z reader, built as the program builds it
set(LZMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/lzma/C)
file(GLOB LZMA_SOURCES ${LZMA_DIR}/*.c)
add_library(xplzma STATIC ${LZMA_SOURCES})
target_include_directories(xplzma PUBLIC ${LZMA_DIR})
target_compile_definitions(xplzma PRIVATE Z7_PPMD_SUPPORT Z7_EXTRACT_ONLY)

add_executable(sysnext-unpack unpack.cpp ../../src/xcore/sysnext_unpack.cpp ../../src/xcore/archive.cpp)
target_link_libraries(sysnext-unpack PRIVATE Qt${QTVERSION}::Core ZLIB::ZLIB xplzma)
if(MINGW)
	target_link_options(sysnext-unpack PRIVATE -static-libgcc -static-libstdc++)
endif()

enable_testing()
add_test(NAME sysnext-unpack
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/unpack.py $<TARGET_FILE:sysnext-unpack>)
