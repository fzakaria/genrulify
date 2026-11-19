cmake_minimum_required(VERSION 3.20)
project(genrulify LANGUAGES C CXX)
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

foreach(tool BASH COREUTILS NIX BWRAP TAR STRACE)
  set(SPLICE_${tool} "$ENV{SPLICE_${tool}}")
  if(NOT SPLICE_${tool})
    message(FATAL_ERROR "Configure inside nix develop for the pinned tool paths")
  endif()
endforeach()
configure_file(src/tools.h.in tools.h @ONLY)

# The sandbox script is configured with the same pinned tools and then
# embedded in the compiler, which writes it into every workspace.
configure_file(src/sandbox.sh.in sandbox.sh @ONLY)
file(READ ${CMAKE_CURRENT_BINARY_DIR}/sandbox.sh SANDBOX_SCRIPT)
configure_file(src/sandbox_script.h.in sandbox_script.h @ONLY)

# The rule definitions are embedded the same way. A change to either file
# has to configure again, which reading a file does not arrange by itself.
file(READ ${CMAKE_SOURCE_DIR}/src/genrulify.bzl BAZEL_RULES)
file(READ ${CMAKE_SOURCE_DIR}/src/genrulify.buck.bzl BUCK_RULES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  src/genrulify.bzl src/genrulify.buck.bzl)
configure_file(src/rules_text.h.in rules_text.h @ONLY)
add_executable(genrulify
  src/main.cpp
  src/dialect.cpp
  src/entry.cpp
  src/process.cpp
  src/recording.cpp
  src/trace.cpp
  src/record.cpp
  src/action.cpp
  src/import.cpp
  src/passes.cpp
  src/convert.cpp
  src/rules.cpp
  src/check.cpp
  src/access.cpp
  src/emit.cpp
  src/describe.cpp
  src/origins.cpp
  src/verify.cpp)
add_dependencies(genrulify SpliceIncGen)
target_link_libraries(genrulify PRIVATE MLIR LLVM absl::strings nlohmann_json::nlohmann_json)
target_compile_options(genrulify PRIVATE -Wall -Wextra -Wno-unused-parameter)
install(TARGETS genrulify)
enable_testing()
foreach(test check-ir cuts groups origins import opt emit rules reference sandbox learn access convert project)
  add_test(NAME ${test} COMMAND bash ${CMAKE_SOURCE_DIR}/tests/${test}.sh $<TARGET_FILE:genrulify> ${CMAKE_SOURCE_DIR}/tests/data)
endforeach()
