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

// Tests folding and canonicalization of assignment ops.

// RUN: iree-opt -split-input-file -pass-pipeline='vm.module(canonicalize)' %s | FileCheck %s --enable-var-scope --dump-input=fail

// CHECK-LABEL: @select_i32_folds
vm.module @select_i32_folds {
  // CHECK-LABEL: @select_i32_zero
  vm.func @select_i32_zero(%arg0 : i32, %arg1 : i32) -> i32 {
    // CHECK: vm.return %arg1 : i32
    %zero = vm.const.i32.zero : i32
    %0 = vm.select.i32 %zero, %arg0, %arg1 : i32
    vm.return %0 : i32
  }

  // CHECK-LABEL: @select_i32_one
  vm.func @select_i32_one(%arg0 : i32, %arg1 : i32) -> i32 {
    // CHECK: vm.return %arg0 : i32
    %c123 = vm.const.i32 123 : i32
    %0 = vm.select.i32 %c123, %arg0, %arg1 : i32
    vm.return %0 : i32
  }

  // CHECK-LABEL: @select_i32_eq
  vm.func @select_i32_eq(%arg0 : i32, %arg1 : i32) -> i32 {
    // CHECK: vm.return %arg1 : i32
    %0 = vm.select.i32 %arg0, %arg1, %arg1 : i32
    vm.return %0 : i32
  }
}

// -----

// CHECK-LABEL: @select_ref_folds
vm.module @select_ref_folds {
  // CHECK-LABEL: @select_ref_zero
  vm.func @select_ref_zero(%arg0 : !ireex.opaque_ref,
                           %arg1 : !ireex.opaque_ref) -> !ireex.opaque_ref {
    // CHECK: vm.return %arg1 : !ireex.opaque_ref
    %zero = vm.const.i32.zero : i32
    %0 = vm.select.ref %zero, %arg0, %arg1 : !ireex.opaque_ref
    vm.return %0 : !ireex.opaque_ref
  }

  // CHECK-LABEL: @select_ref_one
  vm.func @select_ref_one(%arg0 : !ireex.opaque_ref,
                          %arg1 : !ireex.opaque_ref) -> !ireex.opaque_ref {
    // CHECK: vm.return %arg0 : !ireex.opaque_ref
    %c123 = vm.const.i32 123 : i32
    %0 = vm.select.ref %c123, %arg0, %arg1 : !ireex.opaque_ref
    vm.return %0 : !ireex.opaque_ref
  }

  // CHECK-LABEL: @select_ref_eq
  vm.func @select_ref_eq(%arg0 : i32,
                         %arg1 : !ireex.opaque_ref) -> !ireex.opaque_ref {
    // CHECK: vm.return %arg1 : !ireex.opaque_ref
    %0 = vm.select.ref %arg0, %arg1, %arg1 : !ireex.opaque_ref
    vm.return %0 : !ireex.opaque_ref
  }
}
