set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_MINGW_ROOT /opt/homebrew/opt/mingw-w64/toolchain-x86_64)
set(_MINGW_BIN  /opt/homebrew/bin)

set(CMAKE_C_COMPILER   ${_MINGW_BIN}/x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER ${_MINGW_BIN}/x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  ${_MINGW_BIN}/x86_64-w64-mingw32-windres)
set(CMAKE_AR           ${_MINGW_BIN}/x86_64-w64-mingw32-ar      CACHE FILEPATH "")
set(CMAKE_RANLIB       ${_MINGW_BIN}/x86_64-w64-mingw32-ranlib  CACHE FILEPATH "")
set(CMAKE_STRIP        ${_MINGW_BIN}/x86_64-w64-mingw32-strip   CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH
    ${_MINGW_ROOT}/x86_64-w64-mingw32
    ${_MINGW_ROOT})
# x86_64 with SSE4.1 (required by MuPDF deskew.c)
set(CMAKE_C_FLAGS_INIT   "-msse4.1" CACHE STRING "")
set(CMAKE_CXX_FLAGS_INIT "-msse4.1" CACHE STRING "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Expose the DLL source directory to CMakeLists for post-build bundling
set(MINGW_DLL_DIR "${_MINGW_ROOT}/x86_64-w64-mingw32/bin"
    CACHE PATH "Directory containing mingw runtime DLLs")
