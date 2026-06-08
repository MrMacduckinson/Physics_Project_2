# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/src"
  "/src/build-linux-arm64/ppc"
  "/src/build-linux-arm64/Memebooru_PPC-prefix"
  "/src/build-linux-arm64/Memebooru_PPC-prefix/tmp"
  "/src/build-linux-arm64/Memebooru_PPC-prefix/src/Memebooru_PPC-stamp"
  "/src/build-linux-arm64/Memebooru_PPC-prefix/src"
  "/src/build-linux-arm64/Memebooru_PPC-prefix/src/Memebooru_PPC-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/src/build-linux-arm64/Memebooru_PPC-prefix/src/Memebooru_PPC-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/src/build-linux-arm64/Memebooru_PPC-prefix/src/Memebooru_PPC-stamp${cfgdir}") # cfgdir has leading slash
endif()
