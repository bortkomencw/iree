// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_DIALECT_UTIL_IR_UTIL_ATTRS
#define IREE_DIALECT_UTIL_IR_UTIL_ATTRS

include "iree/compiler/Dialect/Util/IR/UtilBase.td"
include "iree/compiler/Dialect/Util/IR/UtilInterfaces.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/OpBase.td"
include "mlir/IR/SubElementInterfaces.td"

//===----------------------------------------------------------------------===//
// Buffer attributes
//===----------------------------------------------------------------------===//

def Util_ByteRangeAttr : AttrDef<Util_Dialect, "ByteRange", []> {
  let mnemonic = "byte_range";
  let summary = [{defines a range of bytes}];
  let description = [{
    Specifies a starting offset and total length in bytes.
  }];

  let parameters = (ins
    AttrParameter<"int64_t", "">:$offset,
    AttrParameter<"int64_t", "">:$length
  );

  let hasCustomAssemblyFormat = 1;
}

def Util_CompositeAttr : AttrDef<Util_Dialect, "Composite", [
  DeclareAttrInterfaceMethods<SubElementAttrInterface>,
  DeclareAttrInterfaceMethods<Util_SerializableAttrInterface, [
    "serializeToBuffer",
    "serializeToStream",
  ]>,
]> {
  let mnemonic = "composite";
  let summary = [{an attribute composed of a sequence of attributes}];
  let description = [{
    Models a concatenated set of serializable attributes that when combined
    form a single sequence of i8 elements. As each value references the uniqued
    storage of the composite element this attribute is cheap to construct. When
    the full flattened range is required it can be efficiently streamed via the
    SerializableAttrInterface. All values must also be serializable.

    All values are tightly packed. If padding is required it can be inserted as
    splat elements attributes with the padding value (usually 0).
  }];

  let parameters = (ins
    AttrParameter<"int64_t", "">:$totalLength,
    AttrParameter<"ArrayAttr", "">:$values
  );

  let builders = [
    AttrBuilder<(ins
      "ArrayRef<Attribute>":$values
    )>,
  ];

  let extraClassDeclaration = [{
    int64_t getNumElements() const { return getTotalLength(); }
  }];

  let genVerifyDecl = 1;

  let hasCustomAssemblyFormat = 1;
}

#endif  // IREE_DIALECT_UTIL_IR_UTIL_ATTRS
