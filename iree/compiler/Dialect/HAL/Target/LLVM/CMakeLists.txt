################################################################################
# Autogenerated by build_tools/bazel_to_cmake/bazel_to_cmake.py from           #
# iree/compiler/Dialect/HAL/Target/LLVM/BUILD                                  #
#                                                                              #
# Use iree_cmake_extra_content from iree/build_defs.oss.bzl to add arbitrary   #
# CMake-only content.                                                          #
#                                                                              #
# To disable autogeneration for this file entirely, delete this header.        #
################################################################################

if(NOT "${IREE_TARGET_BACKEND_DYLIB_LLVM_AOT}" AND NOT "${IREE_TARGET_BACKEND_WASM_LLVM_AOT}")
  return()
endif()

iree_add_all_subdirs()

iree_cc_library(
  NAME
    LLVM
  HDRS
    "LLVMAOTTarget.h"
    "LibraryBuilder.h"
  SRCS
    "LLVMAOTTarget.cpp"
    "LibraryBuilder.cpp"
  DEPS
    ::LLVMIRPasses
    ::LLVMTargetOptions
    ::LinkerTool
    ::StaticLibraryGenerator
    IREELLVMCPUTargetDeps
    IREELinalgExtDialect
    IREELinalgTransformDialect
    LLVMAnalysis
    LLVMBitReader
    LLVMBitWriter
    LLVMCore
    LLVMLinker
    LLVMSupport
    MLIRArmNeon
    MLIRLLVMIR
    MLIRLLVMToLLVMIRTranslation
    MLIRPDL
    MLIRPDLInterp
    MLIRTargetLLVMIRExport
    iree::compiler::Codegen::Common
    iree::compiler::Codegen::Dialect::IREECodegenDialect
    iree::compiler::Codegen::LLVMCPU
    iree::compiler::Codegen::PassHeaders
    iree::compiler::Codegen::Utils
    iree::compiler::Dialect::HAL::Target
    iree::compiler::Dialect::HAL::Target::LLVM::Builtins
  PUBLIC
)

iree_cc_library(
  NAME
    LLVMIRPasses
  HDRS
    "LLVMIRPasses.h"
  SRCS
    "LLVMIRPasses.cpp"
  DEPS
    ::LLVMTargetOptions
    LLVMAnalysis
    LLVMCore
    LLVMInstrumentation
    LLVMMC
    LLVMPasses
    LLVMSupport
    LLVMTarget
    MLIRSupport
  PUBLIC
)

iree_cc_library(
  NAME
    LLVMTargetOptions
  HDRS
    "LLVMTargetOptions.h"
  SRCS
    "LLVMTargetOptions.cpp"
  DEPS
    LLVMMC
    LLVMPasses
    LLVMSupport
    LLVMTarget
  PUBLIC
)

iree_cc_library(
  NAME
    LinkerTool
  HDRS
    "LinkerTool.h"
  SRCS
    "LinkerTool.cpp"
  DEPS
    LLVMSupport
    iree::compiler::Dialect::HAL::Target::LLVM::internal::LinkerTools_internal
  PUBLIC
)

iree_cc_library(
  NAME
    LinkerTool_hdrs
  HDRS
    "LinkerTool.h"
  DEPS
    ::LLVMTargetOptions
    LLVMCore
    LLVMSupport
    MLIRSupport
  PUBLIC
)

iree_cc_library(
  NAME
    StaticLibraryGenerator
  HDRS
    "StaticLibraryGenerator.h"
  SRCS
    "StaticLibraryGenerator.cpp"
  DEPS
    LLVMSupport
  PUBLIC
)

### BAZEL_TO_CMAKE_PRESERVES_ALL_CONTENT_BELOW_THIS_LINE ###

add_library(IREELLVMCPUTargetDeps INTERFACE)

function(_add_optional_llvm_dep dep)
  if(TARGET ${dep})
    target_link_libraries(IREELLVMCPUTargetDeps INTERFACE ${dep})
  endif()
endfunction()

_add_optional_llvm_dep(LLVMAArch64AsmParser)
_add_optional_llvm_dep(LLVMAArch64CodeGen)
_add_optional_llvm_dep(LLVMARMAsmParser)
_add_optional_llvm_dep(LLVMARMCodeGen)
_add_optional_llvm_dep(LLVMRISCVAsmParser)
_add_optional_llvm_dep(LLVMRISCVCodeGen)
_add_optional_llvm_dep(LLVMWebAssemblyAsmParser)
_add_optional_llvm_dep(LLVMWebAssemblyCodeGen)
_add_optional_llvm_dep(LLVMX86AsmParser)
_add_optional_llvm_dep(LLVMX86CodeGen)
