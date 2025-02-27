// RUN: iree-compile -iree-input-type=mhlo -iree-hal-target-backends=vmvx -iree-mlir-to-vm-bytecode-module %s | iree-benchmark-module --driver=vmvx | FileCheck %s
// RUN: [[ $IREE_VULKAN_DISABLE == 1 ]] || (iree-compile -iree-input-type=mhlo -iree-hal-target-backends=vulkan-spirv -iree-mlir-to-vm-bytecode-module %s | iree-benchmark-module --driver=vulkan | FileCheck %s)

module {
  func.func @foo1() -> tensor<4xf32> {
    %input = util.unfoldable_constant dense<[0.0, 1.0, 2.0, 4.0]> : tensor<4xf32>
    %result = "mhlo.exponential"(%input) : (tensor<4xf32>) -> tensor<4xf32>
    return %result : tensor<4xf32>
  }
  func.func @foo2() -> tensor<4xf32> {
    %input = util.unfoldable_constant dense<[0.0, 1.0, 2.0, 4.0]> : tensor<4xf32>
    %result = "mhlo.abs"(%input) : (tensor<4xf32>) -> tensor<4xf32>
    return %result : tensor<4xf32>
  }
}
// CHECK: BM_foo1
// CHECK: BM_foo2
