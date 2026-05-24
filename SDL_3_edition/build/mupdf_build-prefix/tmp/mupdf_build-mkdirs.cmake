# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/mrmacduckinson/dev/Memebooru/SDL_3_edition/sources/mupdf")
  file(MAKE_DIRECTORY "/Users/mrmacduckinson/dev/Memebooru/SDL_3_edition/sources/mupdf")
endif()
file(MAKE_DIRECTORY
  "/Users/mrmacduckinson/dev/Memebooru/SDL_3_edition/build/mupdf_build-prefix/src/mupdf_build-build"
  "/Users/mrmacduckinson/dev/Memebooru/SDL_3_edition/build/mupdf_build-prefix"
  "/Users/mrmacduckinson/dev/Memebooru/SDL_3_edition/build/mupdf_build-prefix/tmp"
  "/Users/mrmacduckinson/dev/Memebooru/SDL_3_edition/build/mupdf_build-prefix/src/mupdf_build-stamp"
  "/Users/mrmacduckinson/dev/Memebooru/SDL_3_edition/build/mupdf_build-prefix/src"
  "/Users/mrmacduckinson/dev/Memebooru/SDL_3_edition/build/mupdf_build-prefix/src/mupdf_build-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/mrmacduckinson/dev/Memebooru/SDL_3_edition/build/mupdf_build-prefix/src/mupdf_build-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/mrmacduckinson/dev/Memebooru/SDL_3_edition/build/mupdf_build-prefix/src/mupdf_build-stamp${cfgdir}") # cfgdir has leading slash
endif()
