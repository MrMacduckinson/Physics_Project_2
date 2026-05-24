cmake_minimum_required(VERSION 3.16)

# ============================================================================
# Multi-target build options - enable as many as you want
# ============================================================================
option(BUILD_FOR_11_0 "Build for macOS 11.0+ (very modern SDK) (arm64) (sdl12-compat)" ON)
option(BUILD_FOR_10_7 "Build for macOS 10.7+ (10.15 SDK) (x86_64) (sdl12-compat)" ON)
option(BUILD_FOR_10_4 "Build for macOS 10.4-10.14 (10.4u SDK) (i386) (SDL 1.2)" ON)
option(BUILD_FOR_PPC "Build for Mac OS 8.1-10.4 (Retro68 Macintosh Toolbox) (PowerPC) (modified SDL 1.2)" ON)
option(BUILD_FOR_M68K "Build for Mac OS 8.1-??? (Retro68 Macintosh Toolbox) (m68k) (modified SDL 1.2)" ON)

# If this is a multi-build invocation, delegate to ExternalProject
if(NOT DEFINED IS_SUBBUILD)
    set(IS_SUBBUILD OFF)
endif()

if(NOT IS_SUBBUILD)
    # Count how many targets are enabled
    set(_build_count 0)
    if(BUILD_FOR_11_0)
        math(EXPR _build_count "${_build_count} + 1")
    endif()
    if(BUILD_FOR_10_7)
        math(EXPR _build_count "${_build_count} + 1")
    endif()
    if(BUILD_FOR_10_4)
        math(EXPR _build_count "${_build_count} + 1")
    endif()
    if(BUILD_FOR_M68K)
        math(EXPR _build_count "${_build_count} + 1")
    endif()
    if(BUILD_FOR_PPC)
        math(EXPR _build_count "${_build_count} + 1")
    endif()

    if(_build_count GREATER 1)
        # Multi-target build: create wrapper project that builds each target separately
        project(Memebooru_MultiTarget NONE)
        include(ExternalProject)

        set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        set(PARENT_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

        if(BUILD_FOR_11_0)
            ExternalProject_Add(Memebooru_11_0
                SOURCE_DIR "${SOURCE_DIR}"
                BINARY_DIR "${PARENT_BINARY_DIR}/arm64_11.0"
                CMAKE_ARGS
                    -DIS_SUBBUILD=ON
                    -DBUILD_FOR_11_0=ON
                    -DBUILD_FOR_10_7=OFF
                    -DBUILD_FOR_10_4=OFF
                    -DBUILD_FOR_M68K=OFF
                    -DBUILD_FOR_PPC=OFF
                    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                BUILD_ALWAYS ON
                INSTALL_COMMAND ""
            )
        endif()

        if(BUILD_FOR_10_7)
            ExternalProject_Add(Memebooru_10_7
                SOURCE_DIR "${SOURCE_DIR}"
                BINARY_DIR "${PARENT_BINARY_DIR}/x86_64_10.7"
                CMAKE_ARGS
                    -DIS_SUBBUILD=ON
                    -DBUILD_FOR_11_0=OFF
                    -DBUILD_FOR_10_7=ON
                    -DBUILD_FOR_10_4=OFF
                    -DBUILD_FOR_M68K=OFF
                    -DBUILD_FOR_PPC=OFF
                    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                BUILD_ALWAYS ON
                INSTALL_COMMAND ""
            )
        endif()

        if(BUILD_FOR_10_4)
            ExternalProject_Add(Memebooru_10_4
                SOURCE_DIR "${SOURCE_DIR}"
                BINARY_DIR "${PARENT_BINARY_DIR}/i386_10.4"
                CMAKE_ARGS
                    -DIS_SUBBUILD=ON
                    -DBUILD_FOR_11_0=OFF
                    -DBUILD_FOR_10_7=OFF
                    -DBUILD_FOR_10_4=ON
                    -DBUILD_FOR_M68K=OFF
                    -DBUILD_FOR_PPC=OFF
                    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                BUILD_ALWAYS ON
                INSTALL_COMMAND ""
            )
        endif()

        if(BUILD_FOR_M68K)
            ExternalProject_Add(Memebooru_M68K
                SOURCE_DIR "${SOURCE_DIR}"
                BINARY_DIR "${PARENT_BINARY_DIR}/m68k"
                CMAKE_ARGS
                    -DIS_SUBBUILD=ON
                    -DBUILD_FOR_11_0=OFF
                    -DBUILD_FOR_10_7=OFF
                    -DBUILD_FOR_10_4=OFF
                    -DBUILD_FOR_M68K=ON
                    -DBUILD_FOR_PPC=OFF
                    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                BUILD_ALWAYS ON
                INSTALL_COMMAND ""
            )
        endif()

        if(BUILD_FOR_PPC)
            ExternalProject_Add(Memebooru_PPC
                SOURCE_DIR "${SOURCE_DIR}"
                BINARY_DIR "${PARENT_BINARY_DIR}/ppc"
                CMAKE_ARGS
                    -DIS_SUBBUILD=ON
                    -DBUILD_FOR_11_0=OFF
                    -DBUILD_FOR_10_7=OFF
                    -DBUILD_FOR_10_4=OFF
                    -DBUILD_FOR_M68K=OFF
                    -DBUILD_FOR_PPC=ON
                    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                BUILD_ALWAYS ON
                INSTALL_COMMAND ""
            )
        endif()

        # Stop here - the real builds happen in subprojects
        return()
    endif()
    # else: single target build, continue normally
endif()

# ============================================================================
# Single-target build configuration
# ============================================================================

# Configure SDK and architectures *before* project()
if(BUILD_FOR_M68K OR BUILD_FOR_PPC)
    # Retro68 builds don't use standard CMake project setup
    # We'll handle these separately after project() is called
    project(Memebooru NONE)

elseif(BUILD_FOR_10_4)
    set(MACOSX_10_4_SDK "/Library/Developer/CommandLineTools/SDKs/MacOSX10.4u.sdk")
    if(NOT EXISTS "${MACOSX_10_4_SDK}")
        message(FATAL_ERROR "MacOSX10.4u.sdk not found at ${MACOSX_10_4_SDK}")
    endif()

    set(CMAKE_OSX_SYSROOT "${MACOSX_10_4_SDK}" CACHE PATH "Sysroot for macOS 10.4" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.4" CACHE STRING "Deployment target" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "i386" CACHE STRING "Architectures to build for (10.4 SDK Intel)" FORCE)

    # SDL 1.2 build configuration
    set(SDL12_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/library_sources/SDL-1.2")
    set(SDL12_BUILD_DIR "${CMAKE_BINARY_DIR}/SDL-1.2-build")
    set(SDL12_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/SDL-1.2-install")

    if(NOT EXISTS "${SDL12_SOURCE_DIR}/configure")
        message(FATAL_ERROR "SDL 1.2 source not found at ${SDL12_SOURCE_DIR}")
    endif()

elseif(BUILD_FOR_10_7)
    set(MACOSX_10_15_SDK "/Library/Developer/CommandLineTools/SDKs/MacOSX10.15.sdk")
    if(NOT EXISTS "${MACOSX_10_15_SDK}")
        message(FATAL_ERROR "MacOSX10.15.sdk not found at ${MACOSX_10_15_SDK}")
    endif()

    set(CMAKE_OSX_SYSROOT "${MACOSX_10_15_SDK}" CACHE PATH "Sysroot for macOS 10.15" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.7" CACHE STRING "Deployment target" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Architectures to build for (10.15 SDK is x86_64-only)" FORCE)

elseif(BUILD_FOR_11_0)
    # Modern macOS build (11.0+, arm64 preferred)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Deployment target" FORCE)
    # Detect host macOS version and choose architecture accordingly
    if(APPLE)
        execute_process(COMMAND sw_vers -productVersion
                        OUTPUT_VARIABLE _host_macos_ver
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(_preferred_arch "arm64")
        if(_host_macos_ver)
            string(REGEX REPLACE "^([0-9]+).*" "\\1" _host_major "${_host_macos_ver}")
            if(_host_major AND _host_major GREATER_EQUAL 11)
                set(_preferred_arch "arm64")
            else()
                set(_preferred_arch "x86_64")
            endif()
        endif()
        set(CMAKE_OSX_ARCHITECTURES "${_preferred_arch}" CACHE STRING "Architectures to build for" FORCE)
    else()
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Architectures to build for" FORCE)
    endif()

else()
    message(FATAL_ERROR "No build target selected. Please enable at least one of: BUILD_FOR_11_0, BUILD_FOR_10_7, BUILD_FOR_10_4")
endif()

# ============================================================================
# M68K and PPC Retro68 builds - these use Makefiles instead of CMake
# ============================================================================
if(BUILD_FOR_M68K OR BUILD_FOR_PPC)
    # Run make from the source directory so all source files are accessible.
    # BUILD_DIR is passed as an absolute path so outputs land in the cmake binary dir.
    if(BUILD_FOR_M68K)
        add_custom_target(build_m68k ALL
            COMMAND ${CMAKE_MAKE_PROGRAM}
                -f "${CMAKE_CURRENT_SOURCE_DIR}/Makefile.macm68k"
                "BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Building M68K target → ${CMAKE_CURRENT_BINARY_DIR}"
            VERBATIM
        )
    elseif(BUILD_FOR_PPC)
        add_custom_target(build_ppc ALL
            COMMAND ${CMAKE_MAKE_PROGRAM}
                -f "${CMAKE_CURRENT_SOURCE_DIR}/Makefile.mac"
                "BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Building PPC target → ${CMAKE_CURRENT_BINARY_DIR}"
            VERBATIM
        )
    endif()
    return()
endif()

# ============================================================================
# Continue with normal CMake builds (10.4, 10.7, 11.0)
# ============================================================================

project(Memebooru LANGUAGES C OBJC)

if(NOT BUILD_FOR_10_7 AND NOT BUILD_FOR_10_4)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# Per-configuration settings
if(BUILD_FOR_10_4)
    # Put generated libs/includes inside the build tree to avoid changing source
    set(PROJECT_LIB_DIR "${CMAKE_BINARY_DIR}/lib_10_4")
    set(PROJECT_INCLUDE_SDL_DIR "${CMAKE_BINARY_DIR}/include_10_4/SDL")
    set(EXECUTABLE_NAME "Memebooru_10_4")
    set(INCLUDE_BASE_DIR "${CMAKE_BINARY_DIR}/include_10_4")

    # Build SDL 1.2 using ExternalProject
    include(ExternalProject)
    ExternalProject_Add(SDL12_build
            SOURCE_DIR "${SDL12_SOURCE_DIR}"
            BINARY_DIR "${SDL12_BUILD_DIR}"
            CONFIGURE_COMMAND ${SDL12_SOURCE_DIR}/configure
            --prefix=${SDL12_INSTALL_PREFIX}
            --host=i386-apple-darwin
            --disable-shared
            --enable-static
            CC=${CMAKE_C_COMPILER}
            CFLAGS=-isysroot\ ${CMAKE_OSX_SYSROOT}\ -mmacosx-version-min=10.4\ -arch\ i386\ -include\ stddef.h
            LDFLAGS=-isysroot\ ${CMAKE_OSX_SYSROOT}\ -mmacosx-version-min=10.4\ -arch\ i386
            BUILD_COMMAND make
            INSTALL_COMMAND make install
            BUILD_ALWAYS OFF
    )

    set(SDL12_STATIC_LIB "${SDL12_INSTALL_PREFIX}/lib/libSDL.a")
    set(SDL12_INCLUDE_DIR "${SDL12_INSTALL_PREFIX}/include/SDL")

    # Create directories to satisfy CMake's imported target validation
    file(MAKE_DIRECTORY "${SDL12_INSTALL_PREFIX}/lib")
    file(MAKE_DIRECTORY "${SDL12_INSTALL_PREFIX}/include")

    # Ensure project-level lib/include dirs exist for the 10.4 target and copy
    # the SDL 1.2 install into them after the ExternalProject installs.
    file(MAKE_DIRECTORY "${PROJECT_LIB_DIR}")
    file(MAKE_DIRECTORY "${PROJECT_INCLUDE_SDL_DIR}")

    add_custom_target(copy_sdl_10_4 ALL
        DEPENDS SDL12_build
        COMMENT "Copying SDL1.2 install into ${PROJECT_LIB_DIR} and ${PROJECT_INCLUDE_SDL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E echo "Copying SDL1.2 from ${SDL12_INSTALL_PREFIX} to project folders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_LIB_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_INCLUDE_SDL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${SDL12_INSTALL_PREFIX}/lib" "${PROJECT_LIB_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${SDL12_INSTALL_PREFIX}/include/SDL" "${PROJECT_INCLUDE_SDL_DIR}"
    )

    set(SDL12_STATIC_LIB "${SDL12_INSTALL_PREFIX}/lib/libSDL.a")
    set(SDL12_INCLUDE_DIR "${SDL12_INSTALL_PREFIX}/include/SDL")

    # Create directories to satisfy CMake's imported target validation
    file(MAKE_DIRECTORY "${SDL12_INSTALL_PREFIX}/lib")
    file(MAKE_DIRECTORY "${SDL12_INSTALL_PREFIX}/include")


elseif(BUILD_FOR_10_7)
    # x86_64 10.7 target - place build outputs in the build tree
    set(PROJECT_LIB_DIR "${CMAKE_BINARY_DIR}/lib_10_7")
    set(PROJECT_INCLUDE_SDL_DIR "${CMAKE_BINARY_DIR}/include_10_7/SDL")
    set(EXECUTABLE_NAME "Memebooru_10_7")
    set(INCLUDE_BASE_DIR "${CMAKE_BINARY_DIR}/include_10_7")

elseif(BUILD_FOR_11_0)
    # Modern macOS 11.0+ target - place build outputs in the build tree
    set(PROJECT_LIB_DIR "${CMAKE_BINARY_DIR}/lib_11_0")
    set(PROJECT_INCLUDE_SDL_DIR "${CMAKE_BINARY_DIR}/include_11_0/SDL")
    set(EXECUTABLE_NAME "Memebooru_11_0")
    set(INCLUDE_BASE_DIR "${CMAKE_BINARY_DIR}/include_11_0")
else()
    # Fallback: place general build artifacts in build/
    set(PROJECT_LIB_DIR "${CMAKE_BINARY_DIR}/lib")
    set(PROJECT_INCLUDE_SDL_DIR "${CMAKE_BINARY_DIR}/include/SDL")
    set(EXECUTABLE_NAME "Memebooru")
    set(INCLUDE_BASE_DIR "${CMAKE_BINARY_DIR}/include")
endif()

# --- SDL2/sdl12-compat build (for non-10.4 builds) ---
if(NOT BUILD_FOR_10_4)
    set(LIBRARY_SOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/library_sources")
    set(SDL2_SOURCE_DIR "${LIBRARY_SOURCES_DIR}/SDL2")
    set(SDL12COMPAT_SOURCE_DIR "${LIBRARY_SOURCES_DIR}/sdl12-compat")

    if(NOT EXISTS "${SDL2_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "SDL2 source not found at ${SDL2_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${SDL12COMPAT_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "sdl12-compat source not found at ${SDL12COMPAT_SOURCE_DIR}")
    endif()

    set(SDL_TEST OFF CACHE BOOL "Build SDL2 tests" FORCE)
    add_subdirectory("${SDL2_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/SDL2-build")

    if(TARGET SDL2::SDL2)
        set(SDL2_MAIN_TARGET SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
        set(SDL2_MAIN_TARGET SDL2::SDL2-static)
    elseif(TARGET SDL2)
        set(SDL2_MAIN_TARGET SDL2)
    else()
        message(FATAL_ERROR "Unable to find SDL2 CMake target")
    endif()

    get_target_property(SDL2_INCLUDE_DIRS ${SDL2_MAIN_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT SDL2_INCLUDE_DIRS)
        set(SDL2_INCLUDE_DIRS "${SDL2_SOURCE_DIR}/include")
    endif()
    set(SDL2_INCLUDE_DIR "${SDL2_INCLUDE_DIRS}" CACHE PATH "SDL2 include dir for sdl12-compat" FORCE)

    add_subdirectory("${SDL12COMPAT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/sdl12-compat-build")

    if(TARGET SDL)
        set(SDL12COMPAT_TARGET SDL)
    elseif(TARGET SDL12_compat)
        set(SDL12COMPAT_TARGET SDL12_compat)
    else()
        message(FATAL_ERROR "Unable to find sdl12-compat library target")
    endif()

    file(MAKE_DIRECTORY "${PROJECT_LIB_DIR}")
    file(MAKE_DIRECTORY "${PROJECT_INCLUDE_SDL_DIR}")

    add_custom_target(copy_sdl_libs_and_headers ALL
            COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${SDL12COMPAT_TARGET}>" "${PROJECT_LIB_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${SDL2_MAIN_TARGET}>" "${PROJECT_LIB_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SDL12COMPAT_SOURCE_DIR}/include/SDL" "${PROJECT_INCLUDE_SDL_DIR}"
            COMMENT "Copying SDL2 and sdl12-compat libraries and headers"
    )
endif()

# --- Main executable ---
if(BUILD_FOR_10_4)
    add_executable(${EXECUTABLE_NAME} MACOSX_BUNDLE main.c eprintf_stub.c)
    set_source_files_properties(main.c PROPERTIES LANGUAGE OBJC)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE BUILD_FOR_10_4)
elseif(BUILD_FOR_10_7)
    add_executable(${EXECUTABLE_NAME} MACOSX_BUNDLE main.c mac_file_dialog.c)
    set_source_files_properties(mac_file_dialog.c PROPERTIES LANGUAGE OBJC)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE BUILD_FOR_10_7)
elseif(BUILD_FOR_11_0)
    add_executable(${EXECUTABLE_NAME} MACOSX_BUNDLE main.c mac_file_dialog.c)
    set_source_files_properties(mac_file_dialog.c PROPERTIES LANGUAGE OBJC)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE BUILD_FOR_11_0)
else()
    add_executable(${EXECUTABLE_NAME} MACOSX_BUNDLE main.c mac_file_dialog.c)
    set_source_files_properties(mac_file_dialog.c PROPERTIES LANGUAGE OBJC)
endif()

# Set bundle properties
set_target_properties(${EXECUTABLE_NAME} PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Memebooru"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.mrmacduckinson.memebooru"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
)

target_include_directories(${EXECUTABLE_NAME} PRIVATE ${INCLUDE_BASE_DIR})

if(BUILD_FOR_10_4)
    # SDL 1.2 static linking
    add_dependencies(${EXECUTABLE_NAME} SDL12_build)

    add_library(SDL12_static STATIC IMPORTED GLOBAL)
    set_target_properties(SDL12_static PROPERTIES
            IMPORTED_LOCATION "${SDL12_STATIC_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL12_INSTALL_PREFIX}/include"
    )

    target_link_libraries(${EXECUTABLE_NAME} PRIVATE
            SDL12_static
            "-framework Cocoa"
            "-framework Carbon"
            "-framework IOKit"
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-framework AudioUnit"
            "-framework OpenGL"
            "-framework ApplicationServices"
    )

    target_compile_options(${EXECUTABLE_NAME} PRIVATE
            "-isysroot" "${CMAKE_OSX_SYSROOT}"
            "-mmacosx-version-min=10.4"
    )
    target_link_options(${EXECUTABLE_NAME} PRIVATE
            "-isysroot" "${CMAKE_OSX_SYSROOT}"
            "-mmacosx-version-min=10.4"
    )
else()
    add_dependencies(${EXECUTABLE_NAME} copy_sdl_libs_and_headers)

    # SDL_ttf (SDL-1.2 branch) compiled directly into the executable.
    # FreeType is built from the copy bundled inside SDL_ttf's external/ directory
    # so it always matches the target architecture (arm64 or x86_64).
    set(SDL_TTF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/library_sources/SDL_ttf_12")
    if(EXISTS "${SDL_TTF_DIR}/SDL_ttf.c")
        include(ExternalProject)
        set(_ft_src    "${SDL_TTF_DIR}/external/freetype-2.4.11")
        set(_ft_prefix "${CMAKE_BINARY_DIR}/freetype-install")
        set(_ft_cflags "-arch ${CMAKE_OSX_ARCHITECTURES} -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        if(CMAKE_OSX_SYSROOT)
            set(_ft_cflags "${_ft_cflags} -isysroot ${CMAKE_OSX_SYSROOT}")
        endif()
        ExternalProject_Add(FreeType_bundled
            SOURCE_DIR "${_ft_src}"
            INSTALL_DIR "${_ft_prefix}"
            CONFIGURE_COMMAND "${_ft_src}/configure"
                --prefix=<INSTALL_DIR>
                --disable-shared
                --enable-static
                --with-zlib=no
                --without-bzip2
                "CC=${CMAKE_C_COMPILER}"
                "CFLAGS=${_ft_cflags}"
                "LDFLAGS=${_ft_cflags}"
            BUILD_COMMAND make -j4
            INSTALL_COMMAND make install
            BUILD_ALWAYS OFF
        )
        set(_ft_inc  "${_ft_prefix}/include")
        set(_ft_inc2 "${_ft_prefix}/include/freetype2")
        set(_ft_lib  "${_ft_prefix}/lib/libfreetype.a")
        file(MAKE_DIRECTORY "${_ft_inc}")
        file(MAKE_DIRECTORY "${_ft_inc2}")
        target_sources(${EXECUTABLE_NAME} PRIVATE "${SDL_TTF_DIR}/SDL_ttf.c")
        # SDL_ttf.h uses #include "SDL.h" (no SDL/ prefix), so expose the SDL header dir directly
        target_include_directories(${EXECUTABLE_NAME} PRIVATE
            "${SDL_TTF_DIR}" "${PROJECT_INCLUDE_SDL_DIR}"
            "${_ft_inc}" "${_ft_inc2}")
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE "${_ft_lib}")
        target_compile_definitions(${EXECUTABLE_NAME} PRIVATE USE_SDL_TTF)
        add_dependencies(${EXECUTABLE_NAME} FreeType_bundled)
        message(STATUS "SDL_ttf enabled with bundled FreeType 2.4.11")
    else()
        message(STATUS "SDL_ttf source not found at ${SDL_TTF_DIR}, font rendering will use CoreText")
    endif()

    set(SDL12COMPAT_LIB "${PROJECT_LIB_DIR}/libSDL-1.2.0.dylib")
    add_library(SDL12_compat_imported SHARED IMPORTED GLOBAL)
    set_target_properties(SDL12_compat_imported PROPERTIES
            IMPORTED_LOCATION "${SDL12COMPAT_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${INCLUDE_BASE_DIR}"
    )

    set_target_properties(${EXECUTABLE_NAME} PROPERTIES BUILD_RPATH "${PROJECT_LIB_DIR}")

    if(APPLE AND BUILD_FOR_10_7)
        target_compile_options(${EXECUTABLE_NAME} PRIVATE
                "-isysroot" "${CMAKE_OSX_SYSROOT}"
                "-mmacosx-version-min=10.7"
        )
        target_link_options(${EXECUTABLE_NAME} PRIVATE
                "-isysroot" "${CMAKE_OSX_SYSROOT}"
                "-mmacosx-version-min=10.7"
        )
    endif()

    target_link_libraries(${EXECUTABLE_NAME} PRIVATE SDL12_compat_imported "-framework ApplicationServices" "-framework Cocoa")

    # Make the .app self-contained: copy SDL dylibs into Contents/Frameworks of the bundle
    if(APPLE)
        # Where to place frameworks relative to the built executable
        set(_bundle_frameworks_dir "$<TARGET_FILE_DIR:${EXECUTABLE_NAME}>/../Frameworks")

        add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Preparing ${EXECUTABLE_NAME} bundle frameworks at ${_bundle_frameworks_dir}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_bundle_frameworks_dir}"
            # copy the sdl12-compat dylib and SDL2 dylib into the bundle
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${SDL12COMPAT_TARGET}>" "${_bundle_frameworks_dir}/$<TARGET_FILE_NAME:${SDL12COMPAT_TARGET}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${SDL2_MAIN_TARGET}>" "${_bundle_frameworks_dir}/$<TARGET_FILE_NAME:${SDL2_MAIN_TARGET}>"
            # set the dylib id to @rpath/<name>
            COMMAND install_name_tool -id "@rpath/$<TARGET_FILE_NAME:${SDL12COMPAT_TARGET}>" "${_bundle_frameworks_dir}/$<TARGET_FILE_NAME:${SDL12COMPAT_TARGET}>"
            COMMAND install_name_tool -id "@rpath/$<TARGET_FILE_NAME:${SDL2_MAIN_TARGET}>" "${_bundle_frameworks_dir}/$<TARGET_FILE_NAME:${SDL2_MAIN_TARGET}>"
            # change the executable to reference @rpath/<name> instead of the absolute build path
            COMMAND install_name_tool -change "$<TARGET_FILE:${SDL12COMPAT_TARGET}>" "@rpath/$<TARGET_FILE_NAME:${SDL12COMPAT_TARGET}>" "$<TARGET_FILE:${EXECUTABLE_NAME}>"
            COMMAND install_name_tool -change "$<TARGET_FILE:${SDL2_MAIN_TARGET}>" "@rpath/$<TARGET_FILE_NAME:${SDL2_MAIN_TARGET}>" "$<TARGET_FILE:${EXECUTABLE_NAME}>"
            # Ensure the executable will search @executable_path/../Frameworks at runtime
            COMMAND install_name_tool -add_rpath "@executable_path/../Frameworks" "$<TARGET_FILE:${EXECUTABLE_NAME}>"
            COMMENT "Embedding SDL dylibs into ${EXECUTABLE_NAME}.app and fixing up install names"
        )
    endif()
endif()
