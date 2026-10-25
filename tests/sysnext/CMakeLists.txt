cmake_minimum_required(VERSION 3.13...4.1)
project(sysnext-unpack-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QTVERSION 6 CACHE STRING "Qt major version")
find_package(Qt${QTVERSION} REQUIRED COMPONENTS Core)
find_package(ZLIB REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(sysnext-unpack unpack.cpp ../../src/xcore/sysnext_unpack.cpp)
target_link_libraries(sysnext-unpack PRIVATE Qt${QTVERSION}::Core ZLIB::ZLIB)
if(MINGW)
	target_link_options(sysnext-unpack PRIVATE -static-libgcc -static-libstdc++)
endif()

enable_testing()
add_test(NAME sysnext-unpack
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/unpack.py $<TARGET_FILE:sysnext-unpack>)
