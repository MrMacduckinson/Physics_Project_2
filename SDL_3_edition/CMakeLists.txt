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
# Vendored SDL3 + SDL3_ttf, built as SHARED libraries so their bundled
# FreeType/HarfBuzz stay private and do not clash with MuPDF's copies.
# ---------------------------------------------------------------------------
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(SDL_TEST     OFF CACHE BOOL "" FORCE)
set(SDL_TESTS    OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_SHARED   ON  CACHE BOOL "" FORCE)
set(SDL_STATIC   OFF CACHE BOOL "" FORCE)
add_subdirectory("${SOURCES_DIR}/SDL" EXCLUDE_FROM_ALL)

set(SDLTTF_VENDORED ON  CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES  OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL  OFF CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ ON  CACHE BOOL "" FORCE)
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

add_executable(physicsbooru MACOSX_BUNDLE ${APP_SOURCES})
target_include_directories(physicsbooru PRIVATE "${SRC_DIR}")
target_link_libraries(physicsbooru PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    mupdf
    mupdf-third)

if(UNIX)
    target_link_libraries(physicsbooru PRIVATE m)
    if(NOT APPLE)
        find_package(Threads REQUIRED)
        target_link_libraries(physicsbooru PRIVATE Threads::Threads)
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
else()
    add_custom_command(TARGET physicsbooru POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FONT_FILE}" "$<TARGET_FILE_DIR:physicsbooru>/Roboto-Regular.ttf")
endif()
