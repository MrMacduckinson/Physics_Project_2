cmake_minimum_required(VERSION 3.22)
project(physicsbooru C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(SRC_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sources")
set(ASSETS_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/assets")

# ---------------------------------------------------------------------------
# Vendored SDL3 + SDL3_ttf.
# Default: shared libs (macOS/Windows). Linux static build: -DPHYSICSBOORU_STATIC=ON
# produces a single fully-static musl ELF (SDL3 dlopen's X11 at runtime).
# ---------------------------------------------------------------------------
option(PHYSICSBOORU_STATIC "Build a fully-static musl binary (Linux only)" OFF)

if(PHYSICSBOORU_STATIC)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED   OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC   ON  CACHE BOOL "" FORCE)
    # Static build: SDL3_ttf and MuPDF both vendor HarfBuzz — disabling it in
    # SDL3_ttf avoids duplicate-symbol link errors. FreeType alone handles
    # Latin/Cyrillic text; HarfBuzz is only needed for Arabic/Hebrew/Indic.
    set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
else()
    set(BUILD_SHARED_LIBS ON  CACHE BOOL "" FORCE)
    set(SDL_SHARED   ON  CACHE BOOL "" FORCE)
    set(SDL_STATIC   OFF CACHE BOOL "" FORCE)
    set(SDLTTF_HARFBUZZ ON  CACHE BOOL "" FORCE)
endif()
set(SDL_TEST     OFF CACHE BOOL "" FORCE)
set(SDL_TESTS    OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${SOURCES_DIR}/SDL" EXCLUDE_FROM_ALL)

set(SDLTTF_VENDORED ON  CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES  OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL  OFF CACHE BOOL "" FORCE)
add_subdirectory("${SOURCES_DIR}/SDL_ttf" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Vendored MuPDF (static), built via its own Makefile.
# Builds on macOS/Linux directly; on Windows use an MSYS2/MinGW shell (see README).
# ---------------------------------------------------------------------------
include(ExternalProject)
include(ProcessorCount)
ProcessorCount(NPROC)
if(NPROC EQUAL 0)
    set(NPROC 4)
endif()

set(MUPDF_SRC "${SOURCES_DIR}/mupdf")
set(MUPDF_OUT "${CMAKE_BINARY_DIR}/mupdf-out")
set(MUPDF_LIB       "${MUPDF_OUT}/libmupdf.a")
set(MUPDF_THIRD_LIB "${MUPDF_OUT}/libmupdf-third.a")

if(WIN32)
    # Cross-compile MuPDF with the mingw toolchain.
    # OS=Linux suppresses Apple-specific flags; HAVE_OBJCOPY=no uses the
    # pre-generated .c font files instead of the ld -b binary trick.
    ExternalProject_Add(mupdf_build
        SOURCE_DIR        "${MUPDF_SRC}"
        BUILD_IN_SOURCE   1
        CONFIGURE_COMMAND ""
        BUILD_COMMAND     make build=release "OUT=${MUPDF_OUT}"
                          OS=Linux
                          "CC=${CMAKE_C_COMPILER}"
                          "CXX=${CMAKE_CXX_COMPILER}"
                          "AR=${CMAKE_AR}"
                          "RANLIB=${CMAKE_RANLIB}"
                          "XCFLAGS=-msse4.1"
                          HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no
                          HAVE_OBJCOPY=no
                          "-j${NPROC}" libs
        INSTALL_COMMAND   ""
        BUILD_BYPRODUCTS  "${MUPDF_LIB}" "${MUPDF_THIRD_LIB}"
    )
else()
    ExternalProject_Add(mupdf_build
        SOURCE_DIR        "${MUPDF_SRC}"
        BUILD_IN_SOURCE   1
        CONFIGURE_COMMAND ""
        BUILD_COMMAND     make build=release "OUT=${MUPDF_OUT}"
                          HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no
                          "-j${NPROC}" libs
        INSTALL_COMMAND   ""
        BUILD_BYPRODUCTS  "${MUPDF_LIB}" "${MUPDF_THIRD_LIB}"
    )
endif()

add_library(mupdf STATIC IMPORTED GLOBAL)
set_target_properties(mupdf PROPERTIES
    IMPORTED_LOCATION "${MUPDF_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${MUPDF_SRC}/include")
add_dependencies(mupdf mupdf_build)

add_library(mupdf-third STATIC IMPORTED GLOBAL)
set_target_properties(mupdf-third PROPERTIES IMPORTED_LOCATION "${MUPDF_THIRD_LIB}")
add_dependencies(mupdf-third mupdf_build)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
set(APP_SOURCES
    "${SRC_DIR}/main.c"
    "${SRC_DIR}/gfx.c"
    "${SRC_DIR}/views.c"
    "${SRC_DIR}/textedit.c"
    "${SRC_DIR}/store.c"
    "${SRC_DIR}/pdfdoc.c"
    "${SRC_DIR}/util.c"
    "${SRC_DIR}/filedialog.c"
)

if(WIN32)
    add_executable(physicsbooru WIN32 ${APP_SOURCES})
elseif(APPLE)
    add_executable(physicsbooru MACOSX_BUNDLE ${APP_SOURCES})
else()
    add_executable(physicsbooru ${APP_SOURCES})
endif()
target_include_directories(physicsbooru PRIVATE "${SRC_DIR}")
target_link_libraries(physicsbooru PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    mupdf
    mupdf-third)

if(WIN32)
    # SDL3 needs these Windows system libraries
    target_link_libraries(physicsbooru PRIVATE
        winmm ole32 oleaut32 imm32 setupapi version gdi32 ws2_32)
elseif(UNIX)
    target_link_libraries(physicsbooru PRIVATE m)
    if(NOT APPLE)
        find_package(Threads REQUIRED)
        target_link_libraries(physicsbooru PRIVATE Threads::Threads)
        if(PHYSICSBOORU_STATIC)
            # stdc++ required because MuPDF contains C++ translation units.
            # dl required for SDL3's runtime dlopen of X11/Wayland/ALSA.
            target_link_libraries(physicsbooru PRIVATE stdc++ dl)
            # -static only safe on musl (Alpine/Void). glibc + -static is unreliable
            # because glibc internals (NSS, resolvers) use dlopen themselves.
            option(PHYSICSBOORU_MUSL "Pass -static to linker; use only with musl toolchains" OFF)
            if(PHYSICSBOORU_MUSL)
                target_link_options(physicsbooru PRIVATE -static)
            endif()
        else()
            # Shared build: resolve libSDL3.so from the binary's own directory.
            set_target_properties(physicsbooru PROPERTIES
                BUILD_RPATH   "$ORIGIN"
                INSTALL_RPATH "$ORIGIN")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Bundle the font next to the executable (and into the .app Resources on macOS)
# ---------------------------------------------------------------------------
set(FONT_FILE "${ASSETS_DIR}/fonts/Roboto-Regular.ttf")

if(APPLE)
    set_source_files_properties("${FONT_FILE}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(physicsbooru PRIVATE "${FONT_FILE}")
    set_target_properties(physicsbooru PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Physicsbooru"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.physicsbooru.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
elseif(WIN32)
    # Font next to the .exe
    add_custom_command(TARGET physicsbooru POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FONT_FILE}" "$<TARGET_FILE_DIR:physicsbooru>/Roboto-Regular.ttf")
    # SDL3 and SDL3_ttf DLLs
    add_custom_command(TARGET physicsbooru POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>"
                "$<TARGET_FILE_DIR:physicsbooru>/SDL3.dll")
    add_custom_command(TARGET physicsbooru POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3_ttf::SDL3_ttf>"
                "$<TARGET_FILE_DIR:physicsbooru>/SDL3_ttf.dll")
    # MinGW runtime DLLs
    if(NOT DEFINED MINGW_DLL_DIR)
        set(MINGW_DLL_DIR /opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32/bin)
    endif()
    foreach(_dll libwinpthread-1.dll)
        if(EXISTS "${MINGW_DLL_DIR}/${_dll}")
            add_custom_command(TARGET physicsbooru POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${MINGW_DLL_DIR}/${_dll}"
                        "$<TARGET_FILE_DIR:physicsbooru>/${_dll}")
        endif()
    endforeach()
    set(_MINGW_LIB_DIR /opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32/lib)
    foreach(_dll libgcc_s_seh-1.dll libstdc++-6.dll)
        if(EXISTS "${_MINGW_LIB_DIR}/${_dll}")
            add_custom_command(TARGET physicsbooru POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_MINGW_LIB_DIR}/${_dll}"
                        "$<TARGET_FILE_DIR:physicsbooru>/${_dll}")
        endif()
    endforeach()
else()
    add_custom_command(TARGET physicsbooru POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FONT_FILE}" "$<TARGET_FILE_DIR:physicsbooru>/Roboto-Regular.ttf")
endif()
