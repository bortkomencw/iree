################################################################################
# Autogenerated by build_tools/bazel_to_cmake/bazel_to_cmake.py from           #
# iree/compiler/Codegen/Utils/BUILD                                            #
#                                                                              #
# Use iree_cmake_extra_content from iree/build_defs.oss.bzl to add arbitrary   #
# CMake-only content.                                                          #
#                                                                              #
# To disable autogeneration for this file entirely, delete this header.        #
################################################################################

iree_add_all_subdirs()

iree_cc_library(
  NAME
    Utils
  HDRS
    "MarkerUtils.h"
    "Utils.h"
  SRCS
    "MarkerUtils.cpp"
    "Utils.cpp"
  DEPS
    IREELinalgExtDialect
    LLVMSupport
    MLIRIR
    MLIRLinalg
    MLIRLinalgTransforms
    MLIRSupport
    iree::compiler::Codegen::Interfaces::ProcessorOpInterfaces
    iree::compiler::Dialect::Flow::IR
    iree::compiler::Dialect::Flow::IR::PartitionableLoopsInterface
    iree::compiler::Dialect::HAL::IR
  PUBLIC
)

### BAZEL_TO_CMAKE_PRESERVES_ALL_CONTENT_BELOW_THIS_LINE ###
