// RUN: iree-tf-opt -pass-pipeline=iree-tf-saved-model-lower-global-tensors -split-input-file <%s | IreeFileCheck %s

// CHECK-LABEL: module attributes {tf_saved_model.semantics}
module attributes {tf_saved_model.semantics} {

// TODO(silvasean): Verify "type" handling.
// I think when "type" is a partial type that flow will not model it correctly.

// CHECK:  flow.variable [[V:@[a-z_]+]] mutable dense<1.000000e+00> : tensor<1xf32>
// CHECK:  func @f() -> (tensor<?xf32> {tf_saved_model.index_path = []})
// CHECK:    [[T:%.+]] = flow.variable.load [[V]] : tensor<?xf32>
// CHECK:    return [[T]] : tensor<?xf32>

  "tf_saved_model.global_tensor"() { is_mutable, sym_name = "v", type = tensor<?xf32>, value = dense<1.> : tensor<1xf32> } : () -> ()
  func @f(%arg0: tensor<!tf.resource<tensor<?xf32>>> {tf_saved_model.bound_input = @v})
  -> (tensor<?xf32> {tf_saved_model.index_path = []})
  attributes {tf_saved_model.exported_names = ["f"]} {
    %0 = "tf.ReadVariableOp"(%arg0) : (tensor<!tf.resource<tensor<?xf32>>>) -> tensor<?xf32>
    return %0 : tensor<?xf32>
  }
}

// -----

// CHECK-LABEL: module attributes {tf_saved_model.semantics}
module attributes {tf_saved_model.semantics} {

// CHECK:  flow.variable [[V:@[a-z_]+]] mutable dense<1.000000e+00> : tensor<1xf32>
// CHECK:  func @f(%arg0: tensor<?xf32> {tf_saved_model.index_path = [0]})
// CHECK:    flow.variable.store %arg0, [[V]] : tensor<?xf32>
// CHECK:    return

  "tf_saved_model.global_tensor"() { is_mutable, sym_name = "v", type = tensor<?xf32>, value = dense<1.> : tensor<1xf32> } : () -> ()
  func @f(%arg0: tensor<?xf32> {tf_saved_model.index_path = [0]}, %arg1: tensor<!tf.resource<tensor<?xf32>>> {tf_saved_model.bound_input = @v})
  attributes {tf_saved_model.exported_names = ["f"]} {
    "tf.AssignVariableOp"(%arg1, %arg0) : (tensor<!tf.resource<tensor<?xf32>>>, tensor<?xf32>) -> ()
    return
  }
}

// -----

// CHECK-LABEL: module attributes {tf_saved_model.semantics}
module attributes {tf_saved_model.semantics} {

// CHECK:  flow.variable [[V:@[a-z_]+]] dense<1.000000e+00> : tensor<1xf32>
// CHECK:  func @f() -> (tensor<1xf32> {tf_saved_model.index_path = []})
// CHECK:    [[T:%.+]] = flow.variable.load [[V]] : tensor<1xf32>
// CHECK:    return [[T]] : tensor<1xf32>

  "tf_saved_model.global_tensor"() { sym_name = "v", type = tensor<1xf32>, value = dense<1.> : tensor<1xf32> } : () -> ()
  func @f(%arg0: tensor<1xf32> {tf_saved_model.bound_input = @v})
  -> (tensor<1xf32> {tf_saved_model.index_path = []})
  attributes {tf_saved_model.exported_names = ["f"]} {
    return %arg0 : tensor<1xf32>
  }
}
