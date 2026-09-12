# Generates version.h before every build, so a dev build always carries the current date.
# Called from CMakeLists.txt as:
#	cmake -DIN_FILE=... -DOUT_FILE=... -DXVERSION_BASE=... -DXRELEASE=... -P genversion.cmake

string(TIMESTAMP XBUILD_YMD "%Y%m%d")
if (XRELEASE)
	set(XBUILD_DATE "")
	set(XVERSION "${XVERSION_BASE}")
	set(XRELEASE_BUILD 1)
else()
	set(XBUILD_DATE "${XBUILD_YMD}")
	set(XVERSION "${XVERSION_BASE}-dev+${XBUILD_DATE}")
	set(XRELEASE_BUILD 0)
endif()

# write to a temp file and copy it only if the content changed: the date changes
# once a day, so nothing depending on version.h is rebuilt on every build
configure_file(${IN_FILE} ${OUT_FILE}.tmp @ONLY)
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT_FILE}.tmp ${OUT_FILE})
file(REMOVE ${OUT_FILE}.tmp)
