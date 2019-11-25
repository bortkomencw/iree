// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef IREE_COMPILER_TRANSLATION_IREEVM_H_
#define IREE_COMPILER_TRANSLATION_IREEVM_H_

#include "iree/compiler/Dialect/HAL/Target/ExecutableTarget.h"
#include "iree/compiler/Dialect/VM/Target/Bytecode/BytecodeModuleTarget.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/IR/Module.h"
#include "mlir/Support/LogicalResult.h"

namespace mlir {
namespace iree_compiler {

// Performs initial dialect conversion to get the canonical input lowered into
// the IREE execution/dataflow dialect.
//
// This will fail if we cannot support the input yet. The hope is that any
// error that happens after this point is either backend-specific (like
// unsupported SPIR-V lowering) or a bug.
LogicalResult convertToFlowModule(ModuleOp moduleOp);

// Translates an MLIR module containing a set of supported IREE input dialects
// to an IREE VM bytecode module for loading at runtime.
//
// See iree/schemas/bytecode_module_def.fbs for the description of the
// serialized module format.
//
// Exposed via the --iree-mlir-to-vm-bytecode-module translation.
LogicalResult translateFromMLIRToVMBytecodeModule(
    ModuleOp moduleOp, IREE::HAL::ExecutableTargetOptions executableOptions,
    IREE::VM::BytecodeTargetOptions bytecodeOptions, llvm::raw_ostream &output);

// TODO(benvanik): versions with multiple targets, etc.

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_TRANSLATION_IREEVM_H_
