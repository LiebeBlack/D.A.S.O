# D.A.S.O - capa Win32 y sus pruebas.
#
# A diferencia del nucleo, aqui si hace falta Windows: CreateProcessW, IOCP y la
# API de archivos. En Windows se compila con el toolset de MSVC en CI, y con
# MinGW en cualquier máquina con GCC.

add_library(daso_platform STATIC
  src/process.cpp
  src/adb.cpp
  src/manifest_store.cpp
)

target_link_libraries(daso_platform PUBLIC daso_core)
target_include_directories(daso_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(daso_platform PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

if(WIN32)
  target_compile_definitions(daso_platform PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
  )
  target_link_libraries(daso_platform PUBLIC shlwapi uuid)
else()
  message(FATAL_ERROR
    "daso_platform es Win32. En sistemas que no sean Windows, construye solo "
    "el objetivo daso_core con -DDASO_PLATFORM=OFF.")
endif()

# --- Pruebas -----------------------------------------------------------------
# BuildManifest y PlanFromManifest son funciones puras: definen que hace
# "Revertir ultimo cambio". Merecen estar cubiertas.

add_executable(daso_platform_tests
  ${CMAKE_SOURCE_DIR}/src/core/tests/test_main.cpp
  tests/test_manifest_store.cpp
)

target_link_libraries(daso_platform_tests PRIVATE daso_platform)
target_include_directories(daso_platform_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/core/tests
)

add_test(NAME daso_platform COMMAND daso_platform_tests)
set_tests_properties(daso_platform PROPERTIES TIMEOUT 60)
