// RUN: iree-run-mlir --target_backends=interpreter-bytecode %s --output_types=f | FileCheck %s

// CHECK-LABEL: EXEC @tensor
func @tensor() -> tensor<4xf32> {
  %input = constant dense<[1.0, 2.0, 3.0, 4.0]> : tensor<4xf32>
  %result = "xla_hlo.rsqrt"(%input) : (tensor<4xf32>) -> tensor<4xf32>
  return %result : tensor<4xf32>
}
// CHECK: 4xf32=1 0.707107 0.57735 0.5

// -----

// CHECK-LABEL: EXEC @scalar
func @scalar() -> tensor<f32> {
  %input = constant dense<16.0> : tensor<f32>
  %result = "xla_hlo.rsqrt"(%input) : (tensor<f32>) -> tensor<f32>
  return %result : tensor<f32>
}
// CHECK: f32=0.25

// -----

// CHECK-LABEL: EXEC @double
func @double() -> tensor<f64> {
  %input = constant dense<16.0> : tensor<f64>
  %result = "xla_hlo.rsqrt"(%input) : (tensor<f64>) -> tensor<f64>
  return %result : tensor<f64>
}
// CHECK: f64=0.25
