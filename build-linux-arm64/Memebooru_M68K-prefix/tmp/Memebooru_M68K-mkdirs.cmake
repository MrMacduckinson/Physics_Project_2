# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/src"
  "/src/build-linux-arm64/m68k"
  "/src/build-linux-arm64/Memebooru_M68K-prefix"
  "/src/build-linux-arm64/Memebooru_M68K-prefix/tmp"
  "/src/build-linux-arm64/Memebooru_M68K-prefix/src/Memebooru_M68K-stamp"
  "/src/build-linux-arm64/Memebooru_M68K-prefix/src"
  "/src/build-linux-arm64/Memebooru_M68K-prefix/src/Memebooru_M68K-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/src/build-linux-arm64/Memebooru_M68K-prefix/src/Memebooru_M68K-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/src/build-linux-arm64/Memebooru_M68K-prefix/src/Memebooru_M68K-stamp${cfgdir}") # cfgdir has leading slash
endif()
