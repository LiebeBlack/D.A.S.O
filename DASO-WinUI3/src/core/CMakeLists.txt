# D.A.S.O - nucleo (portable, sin Windows SDK)
#
# Esta capa es ISO C++ puro a proposito: compila con cualquier compilador y sus
# pruebas se ejecutan sin Visual Studio, sin Windows SDK y sin adb.

cmake_minimum_required(VERSION 3.24)
project(daso_core LANGUAGES CXX)

# Estandar con cadena de respaldo. `c++20` NO sirve: el manifiesto usa
# std::expected, que es C++23. Verificado con GCC 16.2:
#   c++26 -> OK (75 casos, 6593 comprobaciones, 0 fallos)
#   c++23 -> OK (identico)
# Se prueba en orden y se baja al primero que compile.
set(DASO_CXX_STANDARD "c++26" CACHE STRING "Estandar de C++ para el nucleo")
set_property(CACHE DASO_CXX_STANDARD PROPERTY STRINGS c++26 c++23)

include(CheckCXXCompilerFlag)
set(DASO_STANDARD_RESOLVED "")
foreach(_std IN LISTS DASO_CXX_STANDARD)
  string(MAKE_C_IDENTIFIER "HAS${_std}" _var)
  check_cxx_compiler_flag("-std=${_std}" ${_var})
  if(${_var})
    set(DASO_STANDARD_RESOLVED "${_std}")
    break()
  endif()
endforeach()

if(NOT DASO_STANDARD_RESOLVED)
  message(FATAL_ERROR
    "Ningun estandar soportado (${DASO_CXX_STANDARD}) compila con ${CMAKE_CXX_COMPILER}. "
    "Actualiza el toolset o baja DASO_CXX_STANDARD.")
endif()
message(STATUS "D.A.S.O: nucleo compilando con -std=${DASO_STANDARD_RESOLVED}")

add_library(daso_core STATIC
  src/catalog.cpp
  src/filter.cpp
  src/log_buffer.cpp
  src/manifest.cpp
  src/plan.cpp
)

target_compile_features(daso_core PUBLIC cxx_std_23)
target_compile_options(daso_core PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8 /Zc:__cplusplus>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

target_include_directories(daso_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

set_target_properties(daso_core PROPERTIES
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

# --- Pruebas -----------------------------------------------------------------
# Arranis propio, sin gtest ni Catch2: la promesa de que estas pruebas corren
# en cualquier maquina con un compilador no sobrevive a una dependencia de red.

enable_testing()

add_executable(daso_core_tests
  tests/test_main.cpp
  tests/test_catalog.cpp
  tests/test_filter.cpp
  tests/test_manifest.cpp
  tests/test_plan.cpp
)

target_link_libraries(daso_core_tests PRIVATE daso_core)
target_include_directories(daso_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_options(daso_core_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

add_test(NAME daso_core COMMAND daso_core_tests)
set_tests_properties(daso_core PROPERTIES TIMEOUT 60)
