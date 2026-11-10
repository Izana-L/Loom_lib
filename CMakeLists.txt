cmake_minimum_required(VERSION 3.21)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


project(Loom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


option(LOOM_BUILD_TESTS "Compilar los tests de Loom"
       ${PROJECT_IS_TOP_LEVEL})

option(LOOM_BUILD_BENCHMARKS "Compilar el ejecutable de benchmarks Loom_bench"
       ${PROJECT_IS_TOP_LEVEL})

option(LOOM_INSTALL "Generar las reglas de install de Loom"
       ${PROJECT_IS_TOP_LEVEL})

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Loom_warnings)

include(GNUInstallDirs)

# std::thread necesita la librería de hilos del sistema (pthread en Linux).
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(CODE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/code)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${CODE_DIR}/sources/*.cpp)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS ${CODE_DIR}/headers/*.hpp)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CODE_DIR}/tests/Test_*.cpp)
file(GLOB STRESS_SOURCES CONFIGURE_DEPENDS ${CODE_DIR}/tests/Stress_*.cpp)
file(GLOB_RECURSE BENCH_SOURCES CONFIGURE_DEPENDS ${CODE_DIR}/benchmarks/*.cpp)
file(GLOB_RECURSE BENCH_HEADERS CONFIGURE_DEPENDS ${CODE_DIR}/benchmarks/*.hpp)

message(STATUS "CODE_DIR = ${CODE_DIR}")
message(STATUS "SOURCES  = ${SOURCES}")


add_library(Loom STATIC ${SOURCES} ${HEADERS})

add_library(Loom::Loom ALIAS Loom)

target_include_directories(Loom PUBLIC
  $<BUILD_INTERFACE:${CODE_DIR}/headers>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(Loom PUBLIC cxx_std_20)

target_link_libraries(Loom PUBLIC Threads::Threads)

target_compile_definitions(Loom PRIVATE LOOM_VERSION="${PROJECT_VERSION}")

set_target_properties(Loom PROPERTIES DEBUG_POSTFIX d)
Loom_set_warnings(Loom)

if(LOOM_BUILD_TESTS)
  enable_testing()

  # Cada Test_*.cpp y cada Stress_*.cpp de code/tests es un ejecutable con su main.
  #   Test_*   -> etiqueta "unit":   pruebas funcionales, rápidas.
  #   Stress_* -> etiqueta "stress": muchos hilos y millones de operaciones; con más
  #               tiempo límite porque bajo ThreadSanitizer van entre 5 y 15 veces más lentas.
  # Para ejecutar solo unas:  ctest --preset linux-debug -L unit
  foreach(test_source IN LISTS TEST_SOURCES STRESS_SOURCES)
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_include_directories(${test_name} PRIVATE ${CODE_DIR}/sources
                               ${CODE_DIR}/tests)
    target_link_libraries(${test_name} PRIVATE Loom)
    Loom_set_warnings(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    if(test_name MATCHES "^Stress_")
      set_tests_properties(${test_name} PROPERTIES LABELS "stress" TIMEOUT 600)
    else()
      set_tests_properties(${test_name} PROPERTIES LABELS "unit" TIMEOUT 30)
    endif()
  endforeach()

  add_test(NAME Loom_umbrella_complete
           COMMAND ${CMAKE_COMMAND} -DHEADERS_DIR=${CODE_DIR}/headers -DUMBRELLA=Loom/Loom.hpp -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-umbrella.cmake)
  set_tests_properties(Loom_umbrella_complete PROPERTIES LABELS "isolation")
endif()

if(LOOM_BUILD_BENCHMARKS)
  # Un solo ejecutable con todos los .cpp de code/benchmarks (el arnés y los escenarios).
  add_executable(Loom_bench ${BENCH_SOURCES} ${BENCH_HEADERS})
  target_include_directories(Loom_bench PRIVATE ${CODE_DIR}/benchmarks)
  target_link_libraries(Loom_bench PRIVATE Loom)
  Loom_set_warnings(Loom_bench)

  # Compilador y configuración con los que se ha compilado, para que el arnés los
  # guarde junto a cada medida (una medida sin esos datos no sirve para comparar).
  target_compile_definitions(Loom_bench PRIVATE
    LOOM_BENCH_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    LOOM_BENCH_BUILD_TYPE="$<CONFIG>")

  # "cmake --build <carpeta> --target Loom_bench_run" compila y ejecuta los benchmarks,
  # igual en Windows y en Linux. Se ejecuta desde la raíz del proyecto.
  add_custom_target(Loom_bench_run
    COMMAND Loom_bench
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    USES_TERMINAL
    VERBATIM)
endif()

if(LOOM_INSTALL)
  include(CMakePackageConfigHelpers)


  install(TARGETS Loom
          EXPORT LoomTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})


  install(DIRECTORY ${CODE_DIR}/headers/
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
          FILES_MATCHING PATTERN "*.hpp")

  set(LOOM_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/Loom)

  install(EXPORT LoomTargets
          NAMESPACE Loom::
          DESTINATION ${LOOM_CMAKE_INSTALL_DIR})

  configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/LoomConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/LoomConfig.cmake
    INSTALL_DESTINATION ${LOOM_CMAKE_INSTALL_DIR})

  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/LoomConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)

  install(FILES
          ${CMAKE_CURRENT_BINARY_DIR}/LoomConfig.cmake
          ${CMAKE_CURRENT_BINARY_DIR}/LoomConfigVersion.cmake
          DESTINATION ${LOOM_CMAKE_INSTALL_DIR})
endif()


if(PROJECT_IS_TOP_LEVEL AND CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
  add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/compile_commands.json
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_SOURCE_DIR}/compile_commands.json

    DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json
    COMMENT "Copiando compile_commands.json a la raiz (clangd)"
    VERBATIM)
  add_custom_target(Loom_compile_commands ALL DEPENDS ${CMAKE_SOURCE_DIR}/compile_commands.json)
endif()
