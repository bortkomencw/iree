// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree-dialects/Dialect/LinalgExt/IR/LinalgExtOps.h"
#include "iree-dialects/Dialect/LinalgExt/Passes/Transforms.h"
#include "iree/compiler/Dialect/Flow/Conversion/TensorToFlow/ConvertTensorToFlow.h"
#include "iree/compiler/Dialect/Flow/IR/FlowDialect.h"
#include "iree/compiler/Dialect/Flow/IR/FlowOps.h"
#include "iree/compiler/Dialect/Flow/IR/FlowTypes.h"
#include "iree/compiler/Dialect/Flow/IR/PartitionableLoopsInterface.h"
#include "iree/compiler/Dialect/Flow/Transforms/PassDetail.h"
#include "iree/compiler/Dialect/Flow/Transforms/Passes.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Linalg/Transforms/Transforms.h"
#include "mlir/Dialect/MemRef/Transforms/Passes.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/FunctionInterfaces.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/TypeRange.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/Passes.h"
#include "mlir/Transforms/RegionUtils.h"

#define DEBUG_TYPE "iree-flow-dispatch-linalg-on-tensors"

// TODO(ravishankarm): Prune this list.
static llvm::cl::opt<int> clInlineConstantByteLength(
    "iree-flow-inline-constants-max-byte-length",
    llvm::cl::desc("Maximum byte-length of constant that can be inlined into a "
                   "dispatch region"),
    llvm::cl::init(256));

static llvm::cl::list<int64_t> clLinalgOnTensorsTileSizes(
    "iree-flow-dispatch-linalg-on-tensors-tile-sizes",
    llvm::cl::desc("Comma-separated list of tile sizes for tiling on tensors"));

static const char kRootOpAttr[] = "__root_op__";
static const char kFusionGroupsAttr[] = "__fused_op__";

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Flow {

static unsigned kNumMaxParallelDims = 3;

//===----------------------------------------------------------------------===//
// Root and fusion group attribute handling
//===----------------------------------------------------------------------===//

/// Returns true if an op has a root operation.
static bool hasRootOpAttribute(Operation *op) {
  return static_cast<bool>(op->getAttrOfType<IntegerAttr>(kRootOpAttr));
}
/// Removes root attribute. Asserts if root attribute is not present.
static void removeRootOpAttribute(Operation *op) {
  op->removeAttr(kRootOpAttr);
}
/// Sets the root attribute for an operation. The root attribute needs a number
/// to identify the root. Asserts if root attribute is already set on an
/// operation.
static void setRootAttribute(MLIRContext *context, Operation *op,
                             int64_t rootNumber) {
  assert(!op->hasAttr(kRootOpAttr) &&
         "invalid to update root attribute on an op");
  op->setAttr(kRootOpAttr,
              IntegerAttr::get(IntegerType::get(context, 64), rootNumber));
}
/// Returns the number of the root. Asserts if the operation is not already set
/// as a root.
static int64_t getRootNumber(Operation *op) {
  return op->getAttrOfType<IntegerAttr>(kRootOpAttr).getInt();
}
/// Returns true if an op is part of a fusion group.
static bool hasFusionGroupsAttribute(Operation *op) {
  return static_cast<bool>(op->getAttrOfType<ArrayAttr>(kFusionGroupsAttr));
}
/// Returns the fusion groups for the given `op`.
static SmallVector<int64_t, 1> getFusionGroups(Operation *op) {
  SmallVector<int64_t, 1> fusionGroups = {};
  if (auto fusionGroupsAttr = op->getAttrOfType<ArrayAttr>(kFusionGroupsAttr)) {
    fusionGroups = llvm::to_vector<1>(llvm::map_range(
        fusionGroupsAttr,
        [](Attribute attr) { return attr.cast<IntegerAttr>().getInt(); }));
  }
  return fusionGroups;
}
/// Appends the given `op` to the `newGroups` fusion groups.
static void appendToFusionGroup(Operation *op, ArrayRef<int64_t> newGroups) {
  SmallVector<int64_t, 1> fusionGroups = getFusionGroups(op);
  fusionGroups.append(newGroups.begin(), newGroups.end());
  op->setAttr(kFusionGroupsAttr, Builder(op).getI64ArrayAttr(fusionGroups));
}
/// Returns true if the given `op` is in the `targetGroup` fusion group.
static bool isInFusionGroup(Operation *op, unsigned targetGroup) {
  if (ArrayAttr opGroupAttr = op->getAttrOfType<ArrayAttr>(kFusionGroupsAttr)) {
    return llvm::any_of(opGroupAttr, [&targetGroup](Attribute attr) {
      return attr.cast<IntegerAttr>().getInt() == targetGroup;
    });
  }
  return false;
}
/// Removes the fusion groups attribute.
static void removeFusionGroupsAttribute(Operation *op) {
  op->removeAttr(kFusionGroupsAttr);
}

//===----------------------------------------------------------------------===//
// Utility methods
//===----------------------------------------------------------------------===//

/// Given the `shape` of the computation with the first element being the
/// slowest varying and last element being the fastest warying returns the
/// workload value with
/// - fastest varying dimension first, i.e., x, y, z order
/// - the workload padded to `kNumMaxParallelDims` with ones if needed.
/// The `shape` is expected to be of size less than or equal to
/// `kNumMaxParallelDims`.
static SmallVector<Value, 4> convertToWorkload(OpBuilder &b, Location loc,
                                               ArrayRef<Value> shape) {
  assert(shape.size() <= kNumMaxParallelDims &&
         "workload cannot be more than 3D for now");
  SmallVector<Value, 4> workload = llvm::to_vector<4>(llvm::reverse(shape));
  Value one = b.create<arith::ConstantIndexOp>(loc, 1);
  workload.resize(kNumMaxParallelDims, one);
  return workload;
}

//===----------------------------------------------------------------------===//
// Op property charecterizations
//===----------------------------------------------------------------------===//

/// Operations that are treated as root operations for dispatch region
/// formation.
static bool isRootOp(Operation *op) {
  if (op->getParentOfType<IREE::Flow::DispatchWorkgroupsOp>()) {
    return false;
  }
  // Any Linalg named op or generic op with reduction iterator types is a root
  // op.
  if (auto linalgOp = dyn_cast<linalg::LinalgOp>(op)) {
    if (isa<linalg::GenericOp>(op)) {
      return linalgOp.getNumReductionLoops() != 0;
    }
    return !isa<linalg::FillOp>(op);
  }
  return isa<IREE::LinalgExt::TiledOpInterface>(op) &&
         !isa<tensor::ExtractSliceOp, tensor::InsertSliceOp>(op);
}

/// Operations that are cloned into dispatch regions formed with other
/// operations as roots.
static bool isClonableIntoDispatchOp(Operation *op) {
  // TODO(#8637): `tensor.collapse_shape` and `tensor.expand_shape` are
  // trivially clonable too, but they cause problems
  // with bufferization. Make them clonable when fixed.
  if (isa<arith::IndexCastOp, linalg::InitTensorOp, tensor::CastOp,
          tensor::ExtractOp, tensor::ExtractSliceOp>(op)) {
    return true;
  }
  if (auto constantOp = dyn_cast<arith::ConstantOp>(op)) {
    auto constantValueAttr = constantOp.getValue();
    auto constantType = constantOp.getType();
    if (constantValueAttr.isa<SplatElementsAttr>()) {
      return true;
    } else if (auto denseAttr =
                   constantValueAttr.dyn_cast<DenseElementsAttr>()) {
      auto shapedType = constantOp.getType().cast<ShapedType>();
      uint64_t estimatedByteLength =
          (shapedType.getNumElements() * shapedType.getElementTypeBitWidth()) /
          8;
      return denseAttr.isSplat() ||
             estimatedByteLength <= clInlineConstantByteLength;
    } else if (constantType.isIntOrIndexOrFloat()) {
      return true;
    }
  }
  if (llvm::all_of(op->getOperands(),
                   [&](Value v) { return v.getType().isIntOrFloat(); }) &&
      llvm::all_of(op->getResults(),
                   [&](Value v) { return v.getType().isIntOrFloat(); })) {
    return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// Methods that help creating the dispatch regions
//===----------------------------------------------------------------------===//

// Creates a flow.dispatch.workgroup op without arguments.
// All the necessary operands are transiently captured and rewritten late as
// operands. This greatly simplifies transformations into the resulting op.
static std::pair<IREE::Flow::DispatchWorkgroupsOp, Operation *>
buildOperandLessFlowDispatchWorkgroupOp(PatternRewriter &rewriter, Location loc,
                                        ArrayRef<Value> count, Operation *op,
                                        ValueRange resultDynamicDims) {
  SmallVector<Value> operands, operandDims;
  SmallVector<int64_t> tiedOperands;

  auto dispatchOp = rewriter.create<IREE::Flow::DispatchWorkgroupsOp>(
      loc, count, op->getResultTypes(), resultDynamicDims, operands,
      operandDims, tiedOperands);
  Region &region = dispatchOp.body();
  Block *block = &region.front();

  Operation *clonedOp;
  {
    OpBuilder::InsertionGuard g(rewriter);
    rewriter.setInsertionPointToStart(block);
    clonedOp = rewriter.clone(*op);
    unsigned dynamicDimIdx = 0;
    for (auto it : llvm::zip(clonedOp->getResults(),
                             dispatchOp.body().getArguments().take_back(
                                 clonedOp->getNumResults()))) {
      auto resultType = std::get<0>(it).getType().cast<ShapedType>();
      rewriter.create<IREE::Flow::DispatchTensorStoreOp>(
          loc, std::get<0>(it), std::get<1>(it),
          resultDynamicDims.slice(dynamicDimIdx,
                                  resultType.getNumDynamicDims()));
      dynamicDimIdx += resultType.getNumDynamicDims();
    }
    rewriter.create<IREE::Flow::ReturnOp>(loc);
  }

  LLVM_DEBUG(llvm::dbgs() << "Created dispatchOp shell \n"
                          << *dispatchOp << "\n");
  return {dispatchOp, clonedOp};
}

// Fuses producers marked in the same group recursively.
//
// The impl does not worry about the dispatchOp, operands and arguments are set
// in a post-pattern `legalizeDispatchWorkgroupOperands` function.
// To simplify the implementation of the dispatch region formation, we just
// clone the op that needs to be fused inside the dispatch region and just fuse
// that one. This avoid any concerns related to tensor operands that are only
// used for their DimOp. This is a canonicalization that is more involved than
// necessary across the boundary of regions without captures.
static void pullInProducersInSameGroup(
    PatternRewriter &rewriter, IREE::Flow::DispatchWorkgroupsOp dispatchOp,
    linalg::LinalgOp rootOp, int64_t groupNum) {
  LLVM_DEBUG(llvm::dbgs() << "pull in producers for op: " << rootOp << "\n");

  // Scoped within DispatchWorkgroupOp.
  OpBuilder::InsertionGuard g(rewriter);
  rewriter.setInsertionPointToStart(&dispatchOp.getRegion().front());
  for (auto en : llvm::enumerate(rootOp->getOperands())) {
    if (auto producer = en.value().getDefiningOp<linalg::LinalgOp>()) {
      if (!isInFusionGroup(producer, groupNum)) continue;
      DEBUG_WITH_TYPE(DEBUG_TYPE,
                      llvm::dbgs() << "current producer: " << producer << "\n");

      Operation *fusedProducer = rewriter.clone(*producer);
      rewriter.replaceOpWithinBlock(producer, fusedProducer->getResults(),
                                    &dispatchOp.getRegion().front());
      removeFusionGroupsAttribute(fusedProducer);

      pullInProducersInSameGroup(rewriter, dispatchOp, fusedProducer, groupNum);
    } else if (auto producer = en.value().getDefiningOp<tensor::PadOp>()) {
      DEBUG_WITH_TYPE(DEBUG_TYPE,
                      llvm::dbgs() << "current producer: " << producer << "\n");

      Operation *fusedProducer = rewriter.clone(*producer);
      rewriter.replaceOpWithinBlock(producer, fusedProducer->getResults(),
                                    &dispatchOp.getRegion().front());
    }
  }
}

template <typename OpTy>
static Value buildFlowWorkgroupInfoOp(OpBuilder &b, unsigned dim) {
  return b.template create<OpTy>(b.getInsertionPoint()->getLoc(), dim);
}

/// Reorders the operations in `ops` such that they could be inlined into the
/// dispatch region in that order to satisfy dependencies.
static SmallVector<Operation *> orderOperations(ArrayRef<Operation *> ops) {
  LLVM_DEBUG({
    llvm::dbgs() << "Ops to be inlined :\n";
    for (auto op : ops) {
      llvm::dbgs() << "\t";
      op->print(llvm::dbgs());
      llvm::dbgs() << "\n";
    }
  });

  llvm::SmallMapVector<Operation *, SmallVector<Operation *>, 16>
      insertAfterMap;
  llvm::SetVector<Operation *> opSet(ops.begin(), ops.end());
  llvm::SetVector<Operation *> leafOps(ops.begin(), ops.end());
  // For each operation compute the list of operations in `ops` that use its
  // results. Also compute the operations that form the leafs of the DAG of
  // operations in `ops`.
  for (auto op : ops) {
    for (auto operand : op->getOperands()) {
      auto definingOp = operand.getDefiningOp();
      if (!definingOp || !opSet.count(definingOp)) continue;
      insertAfterMap[definingOp].push_back(op);
      if (leafOps.count(op)) leafOps.remove(op);
    }
  }

  // The leaves are at the head of the ordered list.
  SmallVector<Operation *> orderedOps(leafOps.begin(), leafOps.end());
  orderedOps.reserve(ops.size());
  llvm::SmallPtrSet<Operation *, 16> processed;
  processed.insert(leafOps.begin(), leafOps.end());

  // `readyOps` contains the list of operations that have been just added to the
  // `orderedOps` list. With these marked ready, they might make further
  // operations in `ops` ready as well.
  // The complexity of the algorithm is driven by these
  // - Each operations is added to `readyOps` list at most once, and is removed
  //   after being processed
  // - For every operation in `readyOps` every use of its results (within `ops`)
  //   is looked at once.
  // - For every use, the operands of the user are processed.
  // Assuming operands is O(1), i.e. constant order, the complexity is O(sum of
  // number of uses of each operation). Given that the size of `ops` is at max
  // O(10), and not O(100), this is assumed to be reasonable.
  ArrayRef<Operation *> readyOps(orderedOps);
  size_t startPos = 0;
  while (!readyOps.empty()) {
    auto op = readyOps.front();
    startPos++;
    // Check all uses of `op` within `ops`. If all of the operations that define
    // the operands of the user have been added to `orderedOps`, then the user
    // is ready to be scheduled.
    for (auto insertAfterOp : insertAfterMap[op]) {
      if (processed.count(insertAfterOp)) continue;
      if (llvm::all_of(insertAfterOp->getOperands(), [&](Value operand) {
            Operation *operandDefiningOp = operand.getDefiningOp();
            return !operandDefiningOp || !opSet.count(operandDefiningOp) ||
                   processed.count(operandDefiningOp);
          })) {
        // readyOps.push_back(insertAfterOp);
        orderedOps.push_back(insertAfterOp);
        processed.insert(insertAfterOp);
      }
    }
    readyOps = ArrayRef<Operation *>(orderedOps).drop_front(startPos);
  }

  LLVM_DEBUG({
    llvm::dbgs() << "Ops to be inlined (sorted) : \n";
    for (auto op : orderedOps) {
      llvm::dbgs() << "\t";
      op->print(llvm::dbgs());
      llvm::dbgs() << "\n";
    }
  });
  assert(orderedOps.size() == ops.size() &&
         "ordering of inlined operations failed");
  return orderedOps;
}

/// Checks if the `Value` has a use within the dispatch that is unfusable.
static bool hasUnfusableUseInDispatch(
    Value v, IREE::Flow::DispatchWorkgroupsOp dispatchOp) {
  for (OpOperand &use : v.getUses()) {
    Operation *user = use.getOwner();
    // Ignore uses outside of dispatch workgroups op.
    if (user->getParentOfType<IREE::Flow::DispatchWorkgroupsOp>() != dispatchOp)
      continue;

    // Cannot fuse producer of `dest` with `tensor.insert_slice`.
    if (auto insertSliceUser = dyn_cast<tensor::InsertSliceOp>(user)) {
      if (insertSliceUser.dest() == v) return true;
    }
  }
  return false;
}

/// Computes the values that will eventually be used within the dispatch
/// workgroup op but defined outside the op after all clonable operations are
/// cloned into the region.
static void getUsedValuesDefinedAboveAfterCloningOps(
    OpBuilder &builder, IREE::Flow::DispatchWorkgroupsOp dispatchOp,
    llvm::SetVector<Value> &valuesDefinedAbove) {
  llvm::SmallVector<Operation *> clonedOps;
  llvm::SetVector<Value> visited;
  SmallVector<Value, 4> worklist;
  worklist.assign(valuesDefinedAbove.begin(), valuesDefinedAbove.end());
  valuesDefinedAbove.clear();
  while (!worklist.empty()) {
    Value outsideValue = worklist.pop_back_val();
    if (visited.count(outsideValue)) continue;
    visited.insert(outsideValue);

    Operation *definingOp = outsideValue.getDefiningOp();
    if (!definingOp || !(isClonableIntoDispatchOp(definingOp)) ||
        hasUnfusableUseInDispatch(outsideValue, dispatchOp)) {
      valuesDefinedAbove.insert(outsideValue);
      continue;
    }
    clonedOps.push_back(definingOp);
    worklist.append(definingOp->operand_begin(), definingOp->operand_end());
  }
  // The cloned operations form a DAG. Return the cloned operations so the
  // leaves come first, and can be cloned in-order into the dispatch region.
  clonedOps = orderOperations(clonedOps);

  for (auto clonedOp : reverse(clonedOps)) {
    Operation *clone = builder.clone(*clonedOp);
    for (auto result : llvm::enumerate(clonedOp->getResults())) {
      result.value().replaceUsesWithIf(
          clone->getResult(result.index()), [&](OpOperand &use) {
            return use.getOwner()
                       ->getParentOfType<IREE::Flow::DispatchWorkgroupsOp>() ==
                   dispatchOp;
          });
      valuesDefinedAbove.remove(result.value());
    }
    builder.setInsertionPoint(clone);
  }

  // Reverse the values. This is not for correctness, but more for readability
  // of the IR.
  llvm::SetVector<Value> reversedValues;
  reversedValues.insert(valuesDefinedAbove.rbegin(), valuesDefinedAbove.rend());
  std::swap(reversedValues, valuesDefinedAbove);
}

/// Returns the tied operand for the given `resultArg`. Returns nullptr if error
/// or not found.
static BlockArgument getTiedOperandBlockArgument(BlockArgument resultArg) {
  auto resultArgType =
      resultArg.getType().dyn_cast<IREE::Flow::DispatchTensorType>();
  if (!resultArgType ||
      resultArgType.getAccess() != IREE::Flow::TensorAccess::WriteOnly) {
    return nullptr;
  }
  // Each output block argument should just have one use.
  if (!resultArg.hasOneUse()) return nullptr;

  // And that's a flow.dispatch.output.store op.
  auto storeOp = dyn_cast<IREE::Flow::DispatchTensorStoreOp>(
      (*resultArg.getUses().begin()).getOwner());
  if (!storeOp) return nullptr;

  Operation *tieOp = storeOp.value().getDefiningOp();
  if (!tieOp) return nullptr;

  // TODO(antiagainst): use TiedOpInterface here instead of hardcoding ops
  // when it's available in MLIR core in some form.
  BlockArgument tiedArg =
      TypeSwitch<Operation *, BlockArgument>(tieOp)
          .Case<tensor::InsertSliceOp>([&](tensor::InsertSliceOp insertOp)
                                           -> BlockArgument {
            auto loadOp =
                insertOp.dest()
                    .template getDefiningOp<IREE::Flow::DispatchTensorLoadOp>();
            if (!loadOp) return nullptr;
            return loadOp.source().dyn_cast<BlockArgument>();
          })
          .Case<IREE::Flow::DispatchTensorLoadOp>(
              [&](auto loadOp) -> BlockArgument {
                // Check that there is a single use and that the source is
                // block argument. Single use can potentially be relaxed.
                auto loadArg =
                    loadOp.source().template dyn_cast<BlockArgument>();
                if (!loadArg || !loadArg.hasOneUse() ||
                    loadArg.use_begin()->get() != storeOp.target()) {
                  return nullptr;
                }
                return loadArg;
              })
          .Case<linalg::LinalgOp,
                IREE::LinalgExt::LinalgExtOp>([&](auto linalgLikeOp)
                                                  -> BlockArgument {
            unsigned resultIndex =
                storeOp.value().cast<OpResult>().getResultNumber();
            auto loadOp =
                linalgLikeOp.getOutputTensorOperands()[resultIndex]
                    ->get()
                    .template getDefiningOp<IREE::Flow::DispatchTensorLoadOp>();
            if (!loadOp) return nullptr;
            return loadOp.source().template dyn_cast<BlockArgument>();
          })
          .Default([&](Operation *) -> BlockArgument { return nullptr; });

  if (!tiedArg) {
    return nullptr;
  }

  // CHeck that the type of the tied argument candidate and type of the output
  // match and that the tied argument is readonly.
  auto type = tiedArg.getType().dyn_cast<IREE::Flow::DispatchTensorType>();
  if (!type || type.getAccess() != IREE::Flow::TensorAccess::ReadOnly ||
      type.getElementType() != resultArgType.getElementType() ||
      llvm::any_of(llvm::zip(type.getShape(), resultArgType.getShape()),
                   [](std::tuple<int64_t, int64_t> sizes) {
                     return std::get<0>(sizes) !=
                                IREE::Flow::DispatchTensorType::kDynamicSize &&
                            std::get<1>(sizes) !=
                                IREE::Flow::DispatchTensorType::kDynamicSize &&
                            std::get<0>(sizes) != std::get<1>(sizes);
                   })) {
    return nullptr;
  }
  return tiedArg;
}

/// Modifies `dispatchOp` to attach operand-result tie information when
/// possible.
static void tryToTieOperandsAndResults(
    IREE::Flow::DispatchWorkgroupsOp dispatchOp) {
  Block *block = dispatchOp.getBody(0);
  unsigned numOperands = dispatchOp.getODSOperandIndexAndLength(1).second;

  SmallVector<unsigned> eraseArguments;
  // Go over each result to tie operand when possible, by:
  // 1. Update the tied operand argument to take readwrite tensors.
  // 2. Erase the result argument.
  // 3. Attach the tie information to the DispatchWorkgroupsOp.
  for (auto result : llvm::enumerate(dispatchOp.getResults())) {
    if (dispatchOp.getTiedResultOperand(result.value())) continue;
    BlockArgument outputArgument =
        block->getArgument(numOperands + result.index());
    BlockArgument tiedOperandArgument =
        getTiedOperandBlockArgument(outputArgument);
    if (!tiedOperandArgument) continue;
    auto oldType =
        tiedOperandArgument.getType().cast<IREE::Flow::DispatchTensorType>();
    tiedOperandArgument.setType(IREE::Flow::DispatchTensorType::get(
        IREE::Flow::TensorAccess::ReadWrite, oldType.getShape(),
        oldType.getElementType()));
    outputArgument.replaceAllUsesWith(tiedOperandArgument);
    eraseArguments.push_back(outputArgument.getArgNumber());
    dispatchOp.setTiedResultOperandIndex(result.index(),
                                         tiedOperandArgument.getArgNumber());
  }
  block->eraseArguments(eraseArguments);
}

// After outlining in dispatch region we can rewrite the dispatch ops with
// proper captures.
static LogicalResult legalizeDispatchWorkgroupOperands(
    IREE::Flow::DispatchWorkgroupsOp dispatchOp) {
  Location loc = dispatchOp.getLoc();
  Region &region = dispatchOp.body();
  Block &block = region.front();
  OpBuilder b = OpBuilder::atBlockBegin(&block);

  llvm::SetVector<Value> valuesDefinedAbove;
  mlir::getUsedValuesDefinedAbove(region, valuesDefinedAbove);
  if (valuesDefinedAbove.empty()) return success();

  getUsedValuesDefinedAboveAfterCloningOps(b, dispatchOp, valuesDefinedAbove);
  b.setInsertionPointToStart(&block);

  // Build a map from current operands to arguments.
  std::pair<unsigned, unsigned> operandsIndexAndLength =
      dispatchOp.getODSOperandIndexAndLength(1);
  std::pair<unsigned, unsigned> operandDimsIndexAndLength =
      dispatchOp.getODSOperandIndexAndLength(2);
  llvm::DenseMap<Value, BlockArgument> operandToBBArg;
  for (auto operand : llvm::enumerate(dispatchOp.operands())) {
    operandToBBArg[operand.value()] = block.getArgument(operand.index());
  }

  // Of the values defined above and used in the region, add values that are not
  // operands to the region already.
  unsigned numOperands = operandsIndexAndLength.second;
  unsigned numOperandDims = operandDimsIndexAndLength.second;
  for (auto value : valuesDefinedAbove) {
    BlockArgument bbArg = operandToBBArg.lookup(value);
    bool wasPresent = bbArg != nullptr;
    auto tensorType = value.getType().dyn_cast<RankedTensorType>();
    if (!bbArg) {
      // Create a new basic block argument for this value.
      Type bbArgType = value.getType();
      if (tensorType) {
        bbArgType = IREE::Flow::DispatchTensorType::get(
            TensorAccess::ReadOnly, tensorType.getShape(),
            tensorType.getElementType());
      }
      bbArg = block.insertArgument(numOperands, bbArgType, value.getLoc());
    }

    // Insert the operand if this is not already one.
    if (!wasPresent) {
      unsigned insertIdx = operandsIndexAndLength.first + numOperands;
      dispatchOp->insertOperands(insertIdx, {value});
      operandToBBArg[dispatchOp->getOperand(insertIdx)] = bbArg;
      numOperands++;
    }

    Value repl = bbArg;
    if (!wasPresent && bbArg.getType().isa<IREE::Flow::DispatchTensorType>()) {
      // This dims for this operand does not exist. Add those.
      SmallVector<Value> dynamicDimArgs;
      {
        OpBuilder::InsertionGuard g(b);
        b.setInsertionPoint(dispatchOp);

        // Fast-path for if the value comes from ops that support our dynamic
        // shape interfaces. Otherwise we have to insert tensor.dim ops.
        auto availableDims = IREE::Util::findDynamicDims(value);

        // Add operands/args for each dynamic shape dimension.
        SmallVector<Value> dynamicDimOperands;
        unsigned dynamicDimIdx = 0;
        for (auto dim : llvm::enumerate(tensorType.getShape())) {
          if (dim.value() != ShapedType::kDynamicSize) continue;
          if (availableDims.hasValue()) {
            dynamicDimOperands.push_back(
                availableDims.getValue()[dynamicDimIdx]);
          } else {
            dynamicDimOperands.push_back(b.createOrFold<tensor::DimOp>(
                dispatchOp.getLoc(), value, dim.index()));
          }
          dynamicDimArgs.push_back(
              block.insertArgument(numOperands + dynamicDimIdx,
                                   b.getIndexType(), dispatchOp.getLoc()));
          ++dynamicDimIdx;
        }
        dispatchOp->insertOperands(
            operandsIndexAndLength.first + numOperands + numOperandDims,
            dynamicDimOperands);
        numOperandDims += dynamicDimOperands.size();
        dispatchOp->insertOperands(operandsIndexAndLength.first + numOperands,
                                   dynamicDimOperands);
        numOperands += dynamicDimOperands.size();
      }

      // For arguments of type flow.dispatch.tensor, create a
      // flow.dispatch.tensor.load to get the replacement values.
      repl = b.create<IREE::Flow::DispatchTensorLoadOp>(
          loc, value.getType().cast<RankedTensorType>(), bbArg, dynamicDimArgs);
    }

    value.replaceUsesWithIf(repl, [&](OpOperand &use) {
      return use.getOwner()
                 ->getParentOfType<IREE::Flow::DispatchWorkgroupsOp>() ==
             dispatchOp;
    });
  }

  // Update the `operand_segment_sizes`.
  auto operandSegmentSizes = dispatchOp->getAttrOfType<DenseIntElementsAttr>(
      dispatchOp.operand_segment_sizesAttrName());
  auto newValues = llvm::to_vector<4>(llvm::map_range(
      operandSegmentSizes.getValues<APInt>(),
      [&](APInt val) -> int32_t { return val.getSExtValue(); }));
  newValues[1] = numOperands;
  newValues[2] = numOperandDims;
  auto newAttr =
      DenseIntElementsAttr::get(operandSegmentSizes.getType(), newValues);
  dispatchOp->setAttr(dispatchOp.operand_segment_sizesAttrName(), newAttr);
  return success();
}

static bool hasOnlyDimUses(Operation *op) {
  return llvm::all_of(op->getUsers(), [&](Operation *user) {
    return isa<tensor::DimOp>(user);
  });
}

//===----------------------------------------------------------------------===//
// Patterns that create the dispatch region.
//===----------------------------------------------------------------------===//

template <typename T>
static SmallVector<Range> getLoopRanges(T operation, Location loc,
                                        PatternRewriter &rewriter);

template <>
SmallVector<Range> getLoopRanges<linalg::LinalgOp>(linalg::LinalgOp linalgOp,
                                                   Location loc,
                                                   PatternRewriter &rewriter) {
  return linalgOp.createLoopRanges(rewriter, loc);
}

template <>
SmallVector<Range> getLoopRanges<IREE::LinalgExt::TiledOpInterface>(
    IREE::LinalgExt::TiledOpInterface tilableOp, Location loc,
    PatternRewriter &rewriter) {
  return tilableOp.getIterationDomain(rewriter);
}

template <>
SmallVector<Range> getLoopRanges<tensor::InsertSliceOp>(
    tensor::InsertSliceOp insertSliceOp, Location loc,
    PatternRewriter &rewriter) {
  Value zero = rewriter.create<arith::ConstantIndexOp>(loc, 0);
  Value one = rewriter.create<arith::ConstantIndexOp>(loc, 1);
  Value source = insertSliceOp.source();
  SmallVector<Range> loopRanges(insertSliceOp.getSourceType().getRank(),
                                Range{zero, one, one});
  for (auto dim : llvm::seq<unsigned>(0, loopRanges.size())) {
    loopRanges[dim].size = rewriter.create<tensor::DimOp>(loc, source, dim);
  }
  return loopRanges;
}

template <>
SmallVector<Range> getLoopRanges<tensor::ExtractSliceOp>(
    tensor::ExtractSliceOp sliceOp, Location loc, PatternRewriter &rewriter) {
  Value zero = rewriter.create<arith::ConstantIndexOp>(loc, 0);
  Value one = rewriter.create<arith::ConstantIndexOp>(loc, 1);
  ReifiedRankedShapedTypeDims resultDims;
  (void)sliceOp.reifyResultShapes(rewriter, resultDims);
  return llvm::to_vector(llvm::map_range(resultDims[0], [&](Value v) {
    return Range{zero, v, one};
  }));
}

namespace {
template <typename OpType, template <typename> class Base>
struct CreateDispatchRegionOp : Base<OpType> {
  CreateDispatchRegionOp(MLIRContext *context,
                         const linalg::LinalgTransformationFilter &filter,
                         PatternBenefit benefit = 1)
      : Base<OpType>(context, benefit), transformationFilter(filter) {}

  LogicalResult matchAndRewrite(OpType rootOp,
                                PatternRewriter &rewriter) const override {
    // TODO(ravishankarm): It is getting strange to track when to apply this
    // pattern and when not to. Need to revisit this, with dynamic shape cases
    // in mind.
    if (hasOnlyDimUses(rootOp)) return failure();
    if (rootOp->template getParentOfType<IREE::Flow::DispatchWorkgroupsOp>()) {
      return failure();
    }

    if (failed(transformationFilter.checkAndNotify(rewriter, rootOp))) {
      return failure();
    }

    // Compute workgroup count to use for the dispatch op. These are the ranges
    // of the outermost parallel loops that can be distributed.
    Location loc = rootOp->getLoc();
    SmallVector<Range> loopRanges = getLoopRanges(rootOp, loc, rewriter);

    // TODO: The use of PartitionableLoopsInterface to get the loop bounds
    // of the distributed loop is legacy. This can be controlled purely in the
    // backend.
    auto partitionableLoopsOp =
        dyn_cast<PartitionableLoopsInterface>(rootOp.getOperation());
    if (!partitionableLoopsOp) {
      return rewriter.notifyMatchFailure(
          rootOp, "expected op to implement ParitionableLoopsInterface");
    }
    SmallVector<unsigned> partitionedLoops =
        partitionableLoopsOp.getPartitionableLoops(kNumMaxParallelDims);
    SmallVector<Value> count;
    for (auto dim : partitionedLoops) {
      count.push_back(loopRanges[dim].size);
    }
    auto workload = convertToWorkload(rewriter, loc, count);

    // Capture dynamic result dimensions.
    ReifiedRankedShapedTypeDims resultDims;
    auto rankedShapedTypeOp =
        dyn_cast<ReifyRankedShapedTypeOpInterface>(rootOp.getOperation());
    if (!rankedShapedTypeOp) {
      return rewriter.notifyMatchFailure(
          rootOp,
          "expected op to implement the ReifyRankedShapedTypeOpInterface");
    }
    if (failed(rankedShapedTypeOp.reifyResultShapes(rewriter, resultDims))) {
      return rewriter.notifyMatchFailure(rootOp,
                                         "failed to reify shape of the result");
    }

    SmallVector<Value, 4> resultDynamicDims;
    for (auto output : llvm::enumerate(rootOp->getResults())) {
      auto outputType =
          output.value().getType().template dyn_cast<ShapedType>();
      if (!outputType) continue;
      for (auto dim : llvm::enumerate(outputType.getShape())) {
        if (!ShapedType::isDynamic(dim.value())) continue;
        resultDynamicDims.push_back(resultDims[output.index()][dim.index()]);
      }
    }

    // Create a simple dispatch op with no operands, and not isolated from
    // above.
    auto en = buildOperandLessFlowDispatchWorkgroupOp(
        rewriter, loc, workload, rootOp, resultDynamicDims);
    IREE::Flow::DispatchWorkgroupsOp dispatchOp = en.first;
    Operation *clonedOp = en.second;

    // Scoped within DispatchWorkgroupOp.
    if (hasRootOpAttribute(rootOp)) {
      if (auto clonedLinalgOp = dyn_cast<linalg::LinalgOp>(clonedOp)) {
        pullInProducersInSameGroup(rewriter, dispatchOp, clonedLinalgOp,
                                   getRootNumber(rootOp));
      }
    }
    rewriter.replaceOpWithIf(rootOp, dispatchOp.getResults(),
                             [&](OpOperand &operand) {
                               return !isa<tensor::DimOp>(operand.getOwner());
                             });

    transformationFilter.replaceLinalgTransformationFilter(rewriter, rootOp);
    transformationFilter.replaceLinalgTransformationFilter(rewriter, clonedOp);
    return success();
  }

 private:
  linalg::LinalgTransformationFilter transformationFilter;
};
}  // namespace

//===----------------------------------------------------------------------===//
// Heuristics for fusing dispatchble ops with root ops using tile + fuse.
//===----------------------------------------------------------------------===//

/// For the fusion of root op -> elementwise operation to be bufferized
/// in-place without use of extra memory, the result of the root operation
/// must be able to reuse the buffer for the result of the elementwise
/// operation. This is possible if input and output are accessed using the same
/// indexing map.
// TODO: This restriction can go away if we can vectorize always, but that has
// a long tail of tasks.
static bool canInsOperandTieWithOutsOperand(OpOperand *insOperand) {
  auto linalgOp = dyn_cast<linalg::LinalgOp>(insOperand->getOwner());
  if (!linalgOp) return false;
  AffineMap insOperandIndexingMap = linalgOp.getTiedIndexingMap(insOperand);
  auto canTieWithOutsOperand = [&](OpOperand *outsOperand) {
    if (linalgOp.getTiedIndexingMap(outsOperand) != insOperandIndexingMap) {
      return false;
    }
    // TODO(#8411): Until ops are vectorized (always), we need
    // to check that the elementtype matches for the operands to be tied.
    // For now just doing this check for convolution ops since we expect
    // contraction ops to be vectorized.
    auto producerOp = insOperand->get().getDefiningOp();
    if (isa<linalg::GenericOp, linalg::ConvolutionOpInterface>(producerOp) &&
        insOperand->get().getType().cast<ShapedType>().getElementType() !=
            outsOperand->get().getType().cast<ShapedType>().getElementType()) {
      return false;
    }
    return true;
  };
  return llvm::any_of(linalgOp.getOutputOperands(), canTieWithOutsOperand);
}

/// Checks if the producer and consumer LinalgOps can be fused.
static bool areFusableLinalgOps(OpOperand &use) {
  auto producerOp = use.get().getDefiningOp<linalg::LinalgOp>();
  auto consumerOp = dyn_cast<linalg::LinalgOp>(use.getOwner());
  if (!producerOp || !consumerOp) return false;

  // 1. Producer has a single result.
  if (producerOp->getNumResults() != 1) return false;

  // 2. Consumer is elementwise parallel.
  if (consumerOp.getNumLoops() != consumerOp.getNumParallelLoops())
    return false;

  // 3. In consumer the result of producer is accessed using identity indexing.
  AffineMap consumerIndexingMap = consumerOp.getTiedIndexingMap(&use);
  if (!consumerIndexingMap.isIdentity()) return false;

  // 4. In-place bufferization requirements (for now) require that the use in
  // the consumer
  //    can re-use the buffer for a result.
  return canInsOperandTieWithOutsOperand(&use);
}

/// Returns true if this is a fusable use.
static bool isFusableWithConsumer(OpOperand &use) {
  // Check for linalg producer -> consumer fusion with tile + fuse.
  return areFusableLinalgOps(use);
}

/// Fuses roots with its consumers. If a root is fused with its consumer, it is
/// no more tagged as a root to aid with the dispatch region formation.
static void fuseRootsWithConsumers(MLIRContext *context,
                                   ArrayRef<Operation *> roots) {
  SmallVector<Operation *> workList(roots.begin(), roots.end());
  // Fuse with consumers where possible.
  while (!workList.empty()) {
    Operation *currRoot = workList.pop_back_val();
    assert(hasRootOpAttribute(currRoot) &&
           "unexpected non-root op in worklist");
    if (!currRoot->hasOneUse()) continue;

    // Helper function to make the consumer the root instead of the producer
    // when they are to be fused.
    auto updateRootTo = [&context, &currRoot](Operation *newRoot) {
      int64_t rootNumber = getRootNumber(currRoot);
      setRootAttribute(context, newRoot, rootNumber);
      removeRootOpAttribute(currRoot);
      appendToFusionGroup(currRoot, rootNumber);
    };

    // Analyse the use to see if it is fusable.
    OpOperand &use = *currRoot->use_begin();
    Operation *user = use.getOwner();
    if (hasRootOpAttribute(user) || hasFusionGroupsAttribute(user)) {
      continue;
    }

    if (isFusableWithConsumer(use)) {
      updateRootTo(user);
      workList.push_back(user);
    }
  }
}

/// Some heuristic is needed to fuse a dispatchble op with root operations using
/// tile + fuse. Using some heuristic, each root operation is tagged with an ID
/// (using an IntegerAttr with name `kRootOpAttr`) and all dispatchable ops to
/// be fused with it is tagged with the same ID (using a list of IntegerAttr
/// with name `kFusionGroupsAttr`). Each dispatchable operation can be marked to
/// fuse with multiple root operations (i.e. replicated). For now a very simple
/// heuristic is used below, but the mechanism should be general enough to
/// capture any heuristic.
static unsigned decideFusableLinalgOps(FunctionOpInterface funcOp) {
  unsigned numRootOps = 0;
  MLIRContext *context = funcOp->getContext();
  OpBuilder builder(context);
  for (Block &block : funcOp.getBody()) {
    // Dispatch region formation works by first cloning the root into
    // the dispatch region and then pulling operations in.
    // So procedure here is to
    // - First find the roots
    // - To fuse with consumers make the consumer the root.
    SmallVector<Operation *> roots;
    for (Operation &op : llvm::reverse(block)) {
      // Start with a root operation and fuse its producers.
      if (hasFusionGroupsAttribute(&op) || !isRootOp(&op)) continue;
      unsigned newGroup = numRootOps++;
      setRootAttribute(context, &op, newGroup);

      linalg::OpOperandVector outOperands =
          TypeSwitch<Operation *, linalg::OpOperandVector>(&op)
              .Case<linalg::LinalgOp>([&](auto linalgOp) {
                return linalgOp.getOutputTensorOperands();
              })
              .Default(
                  [&](Operation *) -> linalg::OpOperandVector { return {}; });
      for (OpOperand *operand : outOperands) {
        // Currently only fuse with producer ops that are `LinalgOp`s.
        auto producer = operand->get().getDefiningOp<linalg::LinalgOp>();
        if (!producer) continue;

        // For now only fuse producers that have a single use.
        // TODO(ravishankarm): Producer can be fused if all users are dominated
        // by the consumer. But that requires changing the dispatch region
        // formation in this pass. Do that as a follow up.
        if (!producer->hasOneUse()) continue;

        if (producer.getNumLoops() != producer.getNumParallelLoops()) continue;
        appendToFusionGroup(producer, newGroup);
      }
      roots.push_back(&op);
    }
    roots = llvm::to_vector(llvm::reverse(roots));
    fuseRootsWithConsumers(context, roots);
  }

  // Once all root linalg ops have been tagged, put all remaining generic ops
  // into their own dispatches.
  for (Block &block : funcOp.getBody()) {
    SmallVector<Operation *> roots;
    for (Operation &op : llvm::reverse(block)) {
      // If it is part of a fusion group or root op, ignore it.
      if (hasFusionGroupsAttribute(&op) || hasRootOpAttribute(&op)) continue;
      // Only look for Linalg ops here. Avoid moving `linalg.fill` that aren't
      // fused with anything else into their own dispatches since it is better
      // to convert them to splats.
      if (!isa<linalg::LinalgOp>(op) || isa<linalg::FillOp>(op)) continue;

      unsigned newGroup = numRootOps++;
      setRootAttribute(context, &op, newGroup);
      roots.push_back(&op);
    }
    roots = llvm::to_vector(llvm::reverse(roots));
    fuseRootsWithConsumers(context, roots);
  }

  return numRootOps;
}

namespace {
/// Pass declaration.
struct DispatchLinalgOnTensorsPass
    : public DispatchLinalgOnTensorsBase<DispatchLinalgOnTensorsPass> {
  void getDependentDialects(DialectRegistry &registry) const override {
    registry
        .insert<AffineDialect, IREE::Flow::FlowDialect, linalg::LinalgDialect,
                scf::SCFDialect, tensor::TensorDialect>();
  }
  DispatchLinalgOnTensorsPass() = default;
  DispatchLinalgOnTensorsPass(const DispatchLinalgOnTensorsPass &pass) {}
  void runOnOperation() override;

 private:
  Statistic numDispatches{this, "number of dispatches",
                          "Number of Flow dispatches created"};
};

// Pass to test conversion to flow patterns.
struct ConvertToFlowPass : public ConvertToFlowBase<ConvertToFlowPass> {
  void getDependentDialects(DialectRegistry &registry) const override {
    registry
        .insert<AffineDialect, IREE::Flow::FlowDialect, linalg::LinalgDialect,
                scf::SCFDialect, tensor::TensorDialect>();
  }

  void runOnOperation() override {
    MLIRContext *context = &getContext();
    RewritePatternSet convertToFlowPatterns(context);
    populateTensorToFlowConversionPatterns(context, convertToFlowPatterns);
    memref::populateResolveRankedShapeTypeResultDimsPatterns(
        convertToFlowPatterns);
    if (failed(applyPatternsAndFoldGreedily(
            getOperation(), std::move(convertToFlowPatterns)))) {
      return signalPassFailure();
    }
  }
};

}  // namespace

/// For all ops within `funcOp` tagged as root ops, create dispatch regions.
LogicalResult createDispatchRegionsFromRootOps(mlir::Operation *funcOp,
                                               RewritePatternSet &&patterns) {
  MLIRContext *context = funcOp->getContext();

  // Create the dispatch region, first without the isolate region from above
  // property.
  if (failed(applyPatternsAndFoldGreedily(funcOp, std::move(patterns)))) {
    return failure();
  }

  // Run canonicalization patterns and pattern to resolve tensor.dim of result
  // values into tensor.dim of its operands..
  RewritePatternSet canonicalizationPatterns(context);
  memref::populateResolveRankedShapeTypeResultDimsPatterns(
      canonicalizationPatterns);
  if (failed(applyPatternsAndFoldGreedily(
          funcOp, std::move(canonicalizationPatterns)))) {
    return failure();
  }

  LLVM_DEBUG({
    llvm::dbgs() << "\n--- After dispatch op formation ---\n";
    funcOp->print(llvm::dbgs(), OpPrintingFlags().useLocalScope());
    llvm::dbgs() << "\n\n";
  });

  // After outlining in dispatch region we can rewrite the dispatch ops with
  // proper captures to make it isolated from above.
  if (funcOp
          ->walk([&](IREE::Flow::DispatchWorkgroupsOp op) -> WalkResult {
            return legalizeDispatchWorkgroupOperands(op);
          })
          .wasInterrupted()) {
    return failure();
  }

  LLVM_DEBUG({
    llvm::dbgs() << "\n--- After dispatch op legalization ---\n";
    funcOp->print(llvm::dbgs(), OpPrintingFlags().useLocalScope());
    llvm::dbgs() << "\n\n";
  });

  // Now try to see if we can tie certain results to operands in order to
  // indicate sharing storage. This need to happen here because it needs to
  // access region block arguments for input/output tensors, which aren't
  // available until now.
  funcOp->walk([&](IREE::Flow::DispatchWorkgroupsOp op) {
    tryToTieOperandsAndResults(op);
  });

  LLVM_DEBUG({
    llvm::dbgs() << "\n--- After tieing operands and results ---\n";
    funcOp->print(llvm::dbgs(), OpPrintingFlags().useLocalScope());
    llvm::dbgs() << "\n\n";
  });

  return success();
}

void DispatchLinalgOnTensorsPass::runOnOperation() {
  auto funcOp = getOperation();
  MLIRContext *context = &getContext();
  decideFusableLinalgOps(funcOp);

  LLVM_DEBUG({
    llvm::dbgs() << "\n--- After annotating linalg op fusion scheme ---\n";
    funcOp->print(llvm::dbgs(), OpPrintingFlags().useLocalScope());
    llvm::dbgs() << "\n\n";
  });

  {
    linalg::LinalgTransformationFilter filterForComputeOps(
        [](Operation *op) { return success(hasRootOpAttribute(op)); }, {},
        StringAttr::get(context, "indispatch"));
    filterForComputeOps.setMatchByDefault();
    RewritePatternSet computeOpDispatchPatterns(context);
    computeOpDispatchPatterns.insert<
        CreateDispatchRegionOp<linalg::LinalgOp, OpInterfaceRewritePattern>,
        CreateDispatchRegionOp<IREE::LinalgExt::TiledOpInterface,
                               OpInterfaceRewritePattern>,
        CreateDispatchRegionOp<tensor::InsertSliceOp, OpRewritePattern>>(
        context, filterForComputeOps);
    if (failed(createDispatchRegionsFromRootOps(
            funcOp, std::move(computeOpDispatchPatterns)))) {
      return signalPassFailure();
    }
  }

  LLVM_DEBUG({
    llvm::dbgs() << "\n--- After first step of dispatch region formation ---\n";
    funcOp->print(llvm::dbgs(), OpPrintingFlags().useLocalScope());
    llvm::dbgs() << "\n\n";
  });

  /// Convert remaining ops to Flow ops.
  {
    RewritePatternSet convertToFlowPatterns(context);
    populateTensorToFlowConversionPatterns(context, convertToFlowPatterns);
    memref::populateResolveRankedShapeTypeResultDimsPatterns(
        convertToFlowPatterns);
    IREE::Flow::TensorReshapeOp::getCanonicalizationPatterns(
        convertToFlowPatterns, context);
    if (failed(applyPatternsAndFoldGreedily(
            funcOp, std::move(convertToFlowPatterns)))) {
      return signalPassFailure();
    }
  }

  /// Move yet more remaining ops into dispatch region.

  // Start with just moving the tensor.insert_slice into its dispatch.
  {
    linalg::LinalgTransformationFilter filterForInsertSliceOps(
        ArrayRef<StringAttr>{}, StringAttr::get(context, "indispatch"));
    RewritePatternSet insertSliceOpDispatchPatterns(context);
    insertSliceOpDispatchPatterns.insert<
        CreateDispatchRegionOp<tensor::InsertSliceOp, OpRewritePattern>>(
        context, filterForInsertSliceOps);
    if (failed(createDispatchRegionsFromRootOps(
            funcOp, std::move(insertSliceOpDispatchPatterns)))) {
      return signalPassFailure();
    }
  }

  // Now move all remaining ops that need to be cleaned up.
  {
    linalg::LinalgTransformationFilter filterForCleanupOps(
        ArrayRef<StringAttr>{}, StringAttr::get(context, "indispatch"));
    RewritePatternSet cleanUpDispatchPatterns(context);
    cleanUpDispatchPatterns.insert<
        CreateDispatchRegionOp<tensor::ExtractSliceOp, OpRewritePattern>>(
        context, filterForCleanupOps);
    if (failed(createDispatchRegionsFromRootOps(
            funcOp, std::move(cleanUpDispatchPatterns)))) {
      return signalPassFailure();
    }
  }

  // Finally walk all the ops and remove the attributes
  funcOp.walk([](Operation *op) {
    removeRootOpAttribute(op);
    op->removeAttr(linalg::LinalgTransforms::kLinalgTransformMarker);
  });
}

std::unique_ptr<InterfacePass<mlir::FunctionOpInterface>>
createDispatchLinalgOnTensorsPass() {
  return std::make_unique<DispatchLinalgOnTensorsPass>();
}

std::unique_ptr<Pass> createConvertToFlowPass() {
  return std::make_unique<ConvertToFlowPass>();
}

}  // namespace Flow
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
