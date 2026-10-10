# The quick test tier: libxpeccy as a static library with no Qt and no SDL, and the
# headless benches linked against it. A project of its own, so the app's build never
# sees it:
#
#	cmake -S tests -B build/out/tests
#	cmake --build build/out/tests
#
# tests/regress.py quick does both and runs the benches.

cmake_minimum_required(VERSION 3.13...4.1)
project(xpeccy-tests C CXX)

set(XROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(XLIB ${XROOT}/src/libxpeccy)

include(TestBigEndian)
test_big_endian(BIG_ENDIAN)
if(BIG_ENDIAN)
	set(XENDIAN WORDS_BIG_ENDIAN)
else()
	set(XENDIAN WORDS_LITTLE_ENDIAN)
endif()

# the app's own flags, so the core is compiled as it ships
set(CMAKE_C_FLAGS "-std=gnu99 -Wall -O2 ${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "-std=c++14 -Wall -O2 ${CMAKE_CXX_FLAGS}")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

file(GLOB_RECURSE XLIB_SOURCES ${XLIB}/*.c ${XLIB}/*.cpp)
add_library(xpeccycore STATIC ${XLIB_SOURCES})
target_include_directories(xpeccycore PUBLIC ${XLIB} ${ZLIB_INCLUDE_DIR})
target_compile_definitions(xpeccycore PUBLIC ${XENDIAN})
target_link_libraries(xpeccycore PUBLIC ${ZLIB_LIBRARY} Threads::Threads)

set(BENCHES tsconf baseconf tape)
foreach(BENCH ${BENCHES})
	add_executable(${BENCH}-bench core/${BENCH}/main.c)
	target_link_libraries(${BENCH}-bench xpeccycore)
endforeach()

# GPL v2+ (see tools/z80test/LICENSE): the Z80 sources only, never the library
file(GLOB Z80_SOURCES ${XLIB}/cpu/Z80/*.c)
add_executable(z80-coretest ${XROOT}/tools/z80test/coretest.c ${Z80_SOURCES})

# no MinGW runtime on PATH needed to run them
if(MINGW)
	foreach(BENCH ${BENCHES})
		target_link_options(${BENCH}-bench PRIVATE -static)
	endforeach()
	target_link_options(z80-coretest PRIVATE -static)
endif()
