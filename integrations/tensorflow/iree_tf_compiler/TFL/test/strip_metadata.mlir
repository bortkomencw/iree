// RUN: iree-opt-tflite -split-input-file -verify-diagnostics -pass-pipeline='iree-tflite-strip-module-metadata,func.func(iree-tflite-strip-function-metadata)' %s | FileCheck %s

// CHECK-LABEL: module {
// CHECK-NOT: tf.schema_version
module attributes {tfl.schema_version = 3 : i32} {
  // CHECK: func @main
  // CHECK-NOT: tf.entry_function
  func @main(%arg0: tensor<1x8x8x3xf32>) -> tensor<1x8x8x3xf32> attributes {tf.entry_function = {inputs = "input", outputs = "output"}} {
    // CHECK-NEXT: tfl.add
    %0 = tfl.add %arg0, %arg0 {fused_activation_function = "NONE"} : tensor<1x8x8x3xf32>
    %1 = tfl.add %0, %arg0 {fused_activation_function = "NONE"} : tensor<1x8x8x3xf32>
    return %1 : tensor<1x8x8x3xf32>
  }
}
