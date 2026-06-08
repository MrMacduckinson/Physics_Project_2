# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/src"
  "/src/build-linux-arm64/i386_10.4"
  "/src/build-linux-arm64/Memebooru_10_4-prefix"
  "/src/build-linux-arm64/Memebooru_10_4-prefix/tmp"
  "/src/build-linux-arm64/Memebooru_10_4-prefix/src/Memebooru_10_4-stamp"
  "/src/build-linux-arm64/Memebooru_10_4-prefix/src"
  "/src/build-linux-arm64/Memebooru_10_4-prefix/src/Memebooru_10_4-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/src/build-linux-arm64/Memebooru_10_4-prefix/src/Memebooru_10_4-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/src/build-linux-arm64/Memebooru_10_4-prefix/src/Memebooru_10_4-stamp${cfgdir}") # cfgdir has leading slash
endif()
