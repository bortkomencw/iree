// Copyright 2020 Google LLC
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

#include "iree/hal/llvmjit/registration/driver_module.h"

#include "iree/base/flags.h"
#include "iree/base/status.h"
#include "iree/hal/driver_registry.h"
#include "iree/hal/llvmjit/llvmjit_driver.h"
#include "llvm/Support/TargetSelect.h"

namespace iree {
namespace hal {
namespace llvmjit {

static StatusOr<ref_ptr<Driver>> CreateLLVMJITDriver() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  return make_ref<LLVMJITDriver>();
}

}  // namespace llvmjit
}  // namespace hal
}  // namespace iree

IREE_API_EXPORT iree_status_t IREE_API_CALL
iree_hal_llvmjit_driver_module_register() {
  return ::iree::hal::DriverRegistry::shared_registry()->Register(
      "llvm", ::iree::hal::llvmjit::CreateLLVMJITDriver);
}
