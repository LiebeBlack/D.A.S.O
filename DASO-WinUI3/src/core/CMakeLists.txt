# D.A.S.O - nucleo (portable, sin Windows SDK)
#
# Esta capa es ISO C++ puro a proposito: compila con cualquier compilador y sus
# pruebas se ejecutan sin Visual Studio, sin Windows SDK y sin adb.

cmake_minimum_required(VERSION 3.24)
project(daso_core LANGUAGES CXX)

# Estandar de C++, con cadena de respaldo POR COMPILADOR.
#
# Aqui estuvo el bug que rompio CI: se probaba `-std=c++26`, que es la
# sintaxis de GCC/Clang. MSVC no la acepta; la suya es `/std:c++26preview`, y
# solo existe desde VS 17.14. Con MSVC 19.44 el primer nombre que se reconoce
# suele ser `/std:c++latest`, que es exactamente para lo que sirve la cadena.
#
# Verificado con GCC 16.2:
#   -std=c++26 -> OK (83 casos, 6615 comprobaciones, 0 fallos)
#   -std=c++23 -> OK (identico)
# Por que no c++20: el manifiesto usa std::expected, que es C++23.

include(CheckCXXCompilerFlag)

if(MSVC)
  set(_daso_std_flags /std:c++26preview /std:c++latest /std:c++23preview)
else()
  set(_daso_std_flags -std=c++26 -std=c++23 -std=c++20)
endif()

set(DASO_CXX_STANDARD_FLAG "")
foreach(_flag IN LISTS _daso_std_flags)
  string(MAKE_C_IDENTIFIER "HAS${_flag}" _var)
  check_cxx_compiler_flag("${_flag}" ${_var})
  if(${_var})
    set(DASO_CXX_STANDARD_FLAG "${_flag}")
    break()
  endif()
  message(STATUS "D.A.S.O: ${_flag} no lo admite ${CMAKE_CXX_COMPILER_ID}, se prueba el siguiente")
endforeach()

if(NOT DASO_CXX_STANDARD_FLAG)
  message(FATAL_ERROR
    "Ningun estandar soportado (${_daso_std_flags}) compila con ${CMAKE_CXX_COMPILER}. "
    "Actualiza el toolset.")
endif()
message(STATUS "D.A.S.O: nucleo con ${DASO_CXX_STANDARD_FLAG} (${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION})")

add_library(daso_core STATIC
  src/catalog.cpp
  src/filter.cpp
  src/log_buffer.cpp
  src/manifest.cpp
  src/plan.cpp
)

# El flag de estandar va aqui, NO con target_compile_features(cxx_std_23):
# CMake anade su propio /std: o -std: y, al ser posterior en la linea de
# comandos, el mas bajo gana y se pierde el C++26 detectado arriba.
target_compile_options(daso_core PUBLIC "${DASO_CXX_STANDARD_FLAG}")

target_compile_options(daso_core PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8 /Zc:__cplusplus>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

target_include_directories(daso_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Sin CXX_STANDARD_REQUIRED: CMake no debe intentar negociar el estandar, el
# flag resuelto arriba es la unica fuente de verdad.
set_target_properties(daso_core PROPERTIES
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
