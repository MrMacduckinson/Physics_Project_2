# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/src/sources/mupdf"
  "/src/build-linux/mupdf_build-prefix/src/mupdf_build-build"
  "/src/build-linux/mupdf_build-prefix"
  "/src/build-linux/mupdf_build-prefix/tmp"
  "/src/build-linux/mupdf_build-prefix/src/mupdf_build-stamp"
  "/src/build-linux/mupdf_build-prefix/src"
  "/src/build-linux/mupdf_build-prefix/src/mupdf_build-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/src/build-linux/mupdf_build-prefix/src/mupdf_build-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/src/build-linux/mupdf_build-prefix/src/mupdf_build-stamp${cfgdir}") # cfgdir has leading slash
endif()
