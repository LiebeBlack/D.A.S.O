# D.A.S.O Nativo
#
# `core`   ISO puro: se compila y se prueba en cualquier maquina.
# `platform` Win32: solo en Windows.
# La UI (WinUI 3) NO se construye con CMake; va por MSBuild (daso.sln).

cmake_minimum_required(VERSION 3.24)
project(DASO LANGUAGES CXX)

option(DASO_PLATFORM "Construir la capa Win32 (solo Windows)" ON)

enable_testing()
add_subdirectory(src/core)

if(DASO_PLATFORM AND WIN32)
  add_subdirectory(src/platform)
endif()
