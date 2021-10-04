// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/LinalgExt/IR/TiledOpInterface.h"

#include "llvm/Support/Debug.h"
#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/Dialect/Utils/StaticValueUtils.h"

#define DEBUG_TYPE "iree-tiled-op-interface"

namespace mlir {
namespace iree_compiler {
namespace linalg_ext {

#include "iree/compiler/Dialect/LinalgExt/IR/TiledOpInterface.cpp.inc"

/// Converts an `OpFoldResult` to a `Value` by building a constant op if
/// if the `OpFoldResult` is an `IntegerAttr`.
static Value getValue(OpBuilder &builder, Location loc,
                      OpFoldResult valueOrAttr) {
  if (auto attr = valueOrAttr.dyn_cast<Attribute>()) {
    return builder.create<ConstantIndexOp>(loc,
                                           attr.cast<IntegerAttr>().getInt());
  }
  return valueOrAttr.get<Value>();
}

//===----------------------------------------------------------------------===//
// Interface implementations for external operations.
//===----------------------------------------------------------------------===//

namespace {
struct InsertSliceTiledOpInterface
    : public TiledOpInterface::ExternalModel<InsertSliceTiledOpInterface,
                                             tensor::InsertSliceOp> {
  SmallVector<Value> getDestinationOperands(Operation *op) const {
    SmallVector<Value> dest;
    dest.push_back(cast<tensor::InsertSliceOp>(op).dest());
    return dest;
  }

  SmallVector<StringRef> getLoopIteratorTypes(Operation *op) const {
    auto insertSliceOp = cast<tensor::InsertSliceOp>(op);
    return SmallVector<StringRef>(insertSliceOp.getSourceType().getRank(),
                                  getParallelIteratorTypeName());
  }

  SmallVector<Range> getLoopBounds(Operation *op, OpBuilder &b) const {
    auto insertSliceOp = cast<tensor::InsertSliceOp>(op);
    Value source = insertSliceOp.source();
    RankedTensorType sourceType = insertSliceOp.getSourceType();
    Location loc = op->getLoc();
    Value zero = b.create<ConstantIndexOp>(loc, 0);
    Value one = b.create<ConstantIndexOp>(loc, 1);
    SmallVector<Range> loopBounds(sourceType.getRank(),
                                  Range{zero, nullptr, one});
    for (auto dim :
         llvm::seq<int64_t>(0, insertSliceOp.getSourceType().getRank())) {
      loopBounds[dim].size = b.create<tensor::DimOp>(loc, source, dim);
    }
    return loopBounds;
  }

  Operation *getTiledImplementation(Operation *op, OpBuilder &b,
                                    ValueRange outputs,
                                    ArrayRef<OpFoldResult> offsets,
                                    ArrayRef<OpFoldResult> sizes,
                                    SmallVectorImpl<Value> &results) const {
    auto insertOp = cast<tensor::InsertSliceOp>(op);
    // Compute a subtensor of the source based on the offsets.
    auto opStrides = insertOp.getMixedStrides();
    if (!llvm::all_of(opStrides, [&](OpFoldResult valueOrAttr) {
          Optional<int64_t> intVal = getConstantIntValue(valueOrAttr);
          return intVal && *intVal == 1;
        })) {
      op->emitOpError("unable to tile operation with non-unit stride");
      return nullptr;
    }
    Location loc = insertOp.getLoc();
    auto oneAttr = b.getI64IntegerAttr(1);
    SmallVector<OpFoldResult> strides(offsets.size(), oneAttr);
    auto extractSliceOp = b.create<tensor::ExtractSliceOp>(
        loc, insertOp.source(), offsets, sizes, strides);

    // The offsets for the insert is based on the op offsets plus the offsets of
    // the loops passed in.
    auto opOffsets = insertOp.getMixedOffsets();
    auto opSizes = insertOp.getMixedSizes();
    unsigned offsetIndex = 0;
    ArrayRef<int64_t> sourceShape = insertOp.getSourceType().getShape();
    int64_t destRank = insertOp.getType().getRank();
    SmallVector<OpFoldResult> resultOffsets(destRank);
    SmallVector<OpFoldResult> resultSizes(destRank);
    for (auto opOffset : llvm::enumerate(opOffsets)) {
      // Check for rank-reducing by checking that
      // 1) The corresponding opSize value is 1
      // 2) The current rank of the source is not 1.
      // Then the opOffset is for the rank-reduced dimension. Skip.
      unsigned opOffsetIndex = opOffset.index();
      OpFoldResult opOffsetVal = opOffset.value();
      Optional<int64_t> opSizeVal = getConstantIntValue(opSizes[opOffsetIndex]);
      if (opSizeVal && *opSizeVal == 1 && sourceShape[offsetIndex] != 1) {
        resultOffsets[opOffsetIndex] = opOffsetVal;
        resultSizes[opOffsetIndex] = oneAttr;
        continue;
      }
      OpFoldResult offset = offsets[offsetIndex];
      if (opOffsetVal.is<Attribute>() && offset.is<Attribute>()) {
        resultOffsets[opOffsetIndex] = b.getI64IntegerAttr(
            *getConstantIntValue(opOffsetVal) + *getConstantIntValue(offset));
      } else {
        AffineMap map = AffineMap::get(
            1, 1, {b.getAffineDimExpr(0) + b.getAffineSymbolExpr(0)});
        resultOffsets[opOffsetIndex] =
            b.create<AffineApplyOp>(loc, map,
                                    ValueRange{getValue(b, loc, offset),
                                               getValue(b, loc, opOffsetVal)})
                .getResult();
      }
      resultSizes[opOffsetIndex] = sizes[offsetIndex];
      offsetIndex++;
    }
    SmallVector<OpFoldResult> resultStrides(destRank, oneAttr);
    auto tiledInsertOp = b.create<tensor::InsertSliceOp>(
        loc, extractSliceOp.result(), outputs[0], resultOffsets, resultSizes,
        resultStrides);
    results.push_back(tiledInsertOp.result());
    return extractSliceOp;
  }
};
}  // namespace

void registerTiledOpInterfaceExternalModels(DialectRegistry &registry) {
  LLVM_DEBUG({
    llvm::dbgs() << "Adding tiled op interface for tensor.insert_slice\n";
  });
  registry.addOpInterface<tensor::InsertSliceOp, InsertSliceTiledOpInterface>();
}

}  // namespace linalg_ext
}  // namespace iree_compiler
}  // namespace mlir
