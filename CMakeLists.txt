cmake_minimum_required(VERSION 3.20)
project(nixsplice LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
find_program(MLIR_TABLEGEN_EXE mlir-tblgen REQUIRED)
find_package(MLIR REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED)
find_package(absl REQUIRED)
list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")
include(TableGen)
include(AddLLVM)
include(AddMLIR)
include_directories(SYSTEM ${MLIR_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(LLVM_TARGET_DEFINITIONS src/Splice.td)
mlir_tablegen(Dialect.h.inc -gen-dialect-decls)
mlir_tablegen(Dialect.cpp.inc -gen-dialect-defs)
mlir_tablegen(Types.h.inc -gen-typedef-decls)
mlir_tablegen(Types.cpp.inc -gen-typedef-defs)
mlir_tablegen(Attrs.h.inc -gen-attrdef-decls)
mlir_tablegen(Attrs.cpp.inc -gen-attrdef-defs)
mlir_tablegen(Ops.h.inc -gen-op-decls)
mlir_tablegen(Ops.cpp.inc -gen-op-defs)
add_public_tablegen_target(SpliceIncGen)

foreach(tool BASH COREUTILS NIX BWRAP TAR)
  set(SPLICE_${tool} "$ENV{SPLICE_${tool}}")
  if(NOT SPLICE_${tool})
    message(FATAL_ERROR "Configure inside nix develop for the pinned tool paths")
  endif()
endforeach()
configure_file(src/tools.h.in tools.h @ONLY)
add_executable(nixsplice
  src/main.cpp
  src/dialect.cpp
  src/entry.cpp
  src/process.cpp
  src/recording.cpp
  src/trace.cpp
  src/record.cpp
  src/layer.cpp
  src/plan.cpp
  src/replay.cpp
  src/import.cpp
  src/lower.cpp
  src/check.cpp
  src/emit.cpp
  src/describe.cpp
  src/verify.cpp)
add_dependencies(nixsplice SpliceIncGen)
target_link_libraries(nixsplice PRIVATE MLIR LLVM absl::strings nlohmann_json::nlohmann_json)
target_compile_options(nixsplice PRIVATE -Wall -Wextra -Wno-unused-parameter)
install(TARGETS nixsplice)
enable_testing()
foreach(test check-ir cuts import opt emit reference)
  add_test(NAME ${test} COMMAND bash ${CMAKE_SOURCE_DIR}/tests/${test}.sh $<TARGET_FILE:nixsplice> ${CMAKE_SOURCE_DIR}/tests/data)
endforeach()
