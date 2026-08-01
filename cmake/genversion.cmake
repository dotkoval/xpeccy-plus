# Generates version.h before every build, so a dev build always carries the current date.
# Called from CMakeLists.txt as:
#	cmake -DIN_FILE=... -DOUT_FILE=... -DXVERSION_BASE=... -DXRELEASE=... -P genversion.cmake

if (XRELEASE)
	set(XBUILD_DATE "")
	set(XVERSION "${XVERSION_BASE}")
else()
	string(TIMESTAMP XBUILD_DATE "%Y%m%d")
	set(XVERSION "${XVERSION_BASE}-dev (build ${XBUILD_DATE})")
endif()

# write to a temp file and copy it only if the content changed: the date changes
# once a day, so nothing depending on version.h is rebuilt on every build
configure_file(${IN_FILE} ${OUT_FILE}.tmp @ONLY)
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT_FILE}.tmp ${OUT_FILE})
file(REMOVE ${OUT_FILE}.tmp)
