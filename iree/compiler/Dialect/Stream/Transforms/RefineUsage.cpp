// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <utility>

#include "iree/compiler/Dialect/Stream/Analysis/ResourceUsage.h"
#include "iree/compiler/Dialect/Stream/IR/StreamDialect.h"
#include "iree/compiler/Dialect/Stream/IR/StreamOps.h"
#include "iree/compiler/Dialect/Stream/Transforms/PassDetail.h"
#include "iree/compiler/Dialect/Stream/Transforms/Passes.h"
#include "iree/compiler/Dialect/Util/IR/UtilDialect.h"
#include "iree/compiler/Dialect/Util/IR/UtilOps.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/Debug.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#define DEBUG_TYPE "iree-stream-refine-usage"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Stream {
namespace {

//===----------------------------------------------------------------------===//
// Resource usage query/application patterns
//===----------------------------------------------------------------------===//

// Maps a resource usage bitfield to a resource lifetime.
static Lifetime convertUsageToLifetime(ResourceUsageBitfield usage) {
  if (bitEnumContains(usage, ResourceUsageBitfield::Indirect) ||
      bitEnumContains(usage, ResourceUsageBitfield::External)) {
    return Lifetime::External;
  } else if (bitEnumContains(usage, ResourceUsageBitfield::StagingRead) ||
             bitEnumContains(usage, ResourceUsageBitfield::StagingWrite)) {
    return Lifetime::Staging;
  } else if (bitEnumContains(usage, ResourceUsageBitfield::Constant)) {
    return Lifetime::Constant;
  } else if (bitEnumContains(usage, ResourceUsageBitfield::GlobalRead) ||
             bitEnumContains(usage, ResourceUsageBitfield::GlobalWrite)) {
    return bitEnumContains(usage, ResourceUsageBitfield::Mutated) ||
                   bitEnumContains(usage, ResourceUsageBitfield::GlobalWrite) ||
                   bitEnumContains(usage,
                                   ResourceUsageBitfield::DispatchWrite) ||
                   bitEnumContains(usage,
                                   ResourceUsageBitfield::StagingWrite) ||
                   bitEnumContains(usage, ResourceUsageBitfield::TransferWrite)
               ? Lifetime::Variable
               : Lifetime::Constant;
  } else {
    return Lifetime::Transient;
  }
}

// Returns either the affinity of |op| or nullptr.
static IREE::Stream::AffinityAttr getOpAffinity(Operation *op) {
  if (auto affinityOp = dyn_cast<IREE::Stream::AffinityOpInterface>(op)) {
    return affinityOp.getAffinity();
  }
  return {};
}

// Base pattern type for resource usage refinement.
// The results of the usage analysis are available for use by subclasses.
template <typename OpT>
struct UsageRefinementPattern : public OpRewritePattern<OpT> {
  UsageRefinementPattern(MLIRContext *context, ResourceUsageAnalysis &analysis)
      : OpRewritePattern<OpT>(context), analysis(analysis) {}

  ResourceUsageAnalysis &analysis;

  // Updates the |arg| type to the lifetime derived by analysis, if needed.
  // Returns true if a change was made.
  bool applyArgTransition(BlockArgument arg, PatternRewriter &rewriter) const {
    auto oldType = arg.getType().dyn_cast<IREE::Stream::ResourceType>();
    if (!oldType) return false;
    auto newUsage = analysis.lookupResourceUsage(arg);
    auto newLifetime = convertUsageToLifetime(newUsage);
    auto newType = rewriter.getType<IREE::Stream::ResourceType>(newLifetime);
    if (oldType == newType) {
      // Old and new lifetimes match; no need to apply a transition.
      return false;
    } else if (oldType.getLifetime() != IREE::Stream::Lifetime::Unknown) {
      // Transitioning lifetimes; rely on users to insert the transitions.
      return false;
    } else {
      // Directly overwrite the existing lifetime.
      arg.setType(newType);
      return true;
    }
  }

  // Updates the |result| type to the lifetime derived by analysis, if needed.
  // Returns true if a change was made.
  bool applyResultTransition(Operation *op, Value result,
                             PatternRewriter &rewriter) const {
    auto oldType = result.getType().dyn_cast<IREE::Stream::ResourceType>();
    if (!oldType) return false;
    auto newUsage = analysis.lookupResourceUsage(result);
    auto newLifetime = convertUsageToLifetime(newUsage);
    auto newType = rewriter.getType<IREE::Stream::ResourceType>(newLifetime);
    if (oldType == newType) {
      // Old and new lifetimes match; no need to apply a transition.
      return false;
    } else if (oldType.getLifetime() != IREE::Stream::Lifetime::Unknown) {
      // Transitioning from one lifetime to another; insert a transfer
      // placeholder (as we may later decide it's ok to transition on a
      // particular device).
      auto resultSize = rewriter.createOrFold<IREE::Stream::ResourceSizeOp>(
          op->getLoc(), result);
      auto affinityAttr = getOpAffinity(op);
      auto transferOp = rewriter.create<IREE::Stream::AsyncTransferOp>(
          op->getLoc(), newType, result, resultSize, resultSize,
          /*source_affinity=*/affinityAttr,
          /*target_affinity=*/affinityAttr);
      result.replaceUsesWithIf(transferOp.result(), [&](OpOperand &operand) {
        return operand.getOwner() != transferOp &&
               operand.getOwner() != resultSize.getDefiningOp();
      });
      return true;
    } else {
      // Directly overwrite the existing lifetime.
      result.setType(newType);
      return true;
    }
  }

  // Updates the |result| type to the lifetime derived by analysis, if needed.
  // Returns true if a change was made. Same as above but for when we have the
  // information available and don't need to insert the queries.
  bool applyResultTransition(Value result, Value resultSize,
                             IREE::Stream::AffinityAttr affinityAttr,
                             PatternRewriter &rewriter) const {
    auto oldType = result.getType().dyn_cast<IREE::Stream::ResourceType>();
    if (!oldType) return false;
    auto newUsage = analysis.lookupResourceUsage(result);
    auto newLifetime = convertUsageToLifetime(newUsage);
    auto newType = rewriter.getType<IREE::Stream::ResourceType>(newLifetime);
    if (oldType == newType) {
      // Old and new lifetimes match; no need to apply a transition.
      return false;
    } else if (oldType.getLifetime() != IREE::Stream::Lifetime::Unknown) {
      // Transitioning from one lifetime to another; insert a transfer
      // placeholder (as we may later decide it's ok to transition on a
      // particular device).
      auto transferOp = rewriter.create<IREE::Stream::AsyncTransferOp>(
          result.getLoc(), newType, result, resultSize, resultSize,
          /*source_affinity=*/affinityAttr,
          /*target_affinity=*/affinityAttr);
      result.replaceAllUsesExcept(transferOp.result(), transferOp);
      return true;
    } else {
      // Directly overwrite the existing lifetime.
      result.setType(newType);
      return true;
    }
  }

  // Updates all blocks argument lifetimes within the regions of |op|.
  // Returns true if a change was made.
  bool applyRegionTransitions(Operation *op, PatternRewriter &rewriter) const {
    bool didChange = false;
    rewriter.startRootUpdate(op);
    for (auto &region : op->getRegions()) {
      for (auto &block : region) {
        rewriter.setInsertionPoint(&block, block.begin());
        for (auto &blockArg : block.getArguments()) {
          if (applyArgTransition(blockArg, rewriter)) {
            didChange = true;
          }
        }
      }
    }
    if (didChange) {
      rewriter.finalizeRootUpdate(op);
    } else {
      rewriter.cancelRootUpdate(op);
    }
    return didChange;
  }
};

// Applies usage analysis results to an initializer callable.
// All nested operations will have their lifetime specified.
struct ApplyInitializerOp
    : public UsageRefinementPattern<IREE::Util::InitializerOp> {
  using UsageRefinementPattern<
      IREE::Util::InitializerOp>::UsageRefinementPattern;
  LogicalResult matchAndRewrite(IREE::Util::InitializerOp op,
                                PatternRewriter &rewriter) const override {
    bool didChange = this->applyRegionTransitions(op, rewriter);
    return success(didChange);
  }
};

// Applies usage analysis results to an MLIR function.
// All resource arguments and results, block arguments, and nested operations
// will have their lifetime specified.
struct ApplyFuncOp : public UsageRefinementPattern<mlir::func::FuncOp> {
  using UsageRefinementPattern<mlir::func::FuncOp>::UsageRefinementPattern;
  LogicalResult matchAndRewrite(mlir::func::FuncOp op,
                                PatternRewriter &rewriter) const override {
    bool didChange = false;

    // Arguments:
    SmallVector<Type> newInputs;
    for (auto inputType : llvm::enumerate(op.getFunctionType().getInputs())) {
      auto oldType = inputType.value().dyn_cast<IREE::Stream::ResourceType>();
      if (!oldType) {
        newInputs.push_back(inputType.value());
      } else if (oldType.getLifetime() == IREE::Stream::Lifetime::Unknown) {
        auto blockArg = op.getArgument(inputType.index());
        auto newUsage = analysis.lookupResourceUsage(blockArg);
        auto newLifetime = convertUsageToLifetime(newUsage);
        auto newType =
            rewriter.getType<IREE::Stream::ResourceType>(newLifetime);
        newInputs.push_back(newType);
      } else {
        newInputs.push_back(oldType);
      }
    }

    // Results:
    SmallVector<Type> newOutputs;
    auto anyReturnOp = *op.getOps<mlir::func::ReturnOp>().begin();
    for (auto outputType : llvm::enumerate(op.getFunctionType().getResults())) {
      auto oldType = outputType.value().dyn_cast<IREE::Stream::ResourceType>();
      if (!oldType) {
        newOutputs.push_back(outputType.value());
      } else if (oldType.getLifetime() == IREE::Stream::Lifetime::Unknown) {
        auto returnValue = anyReturnOp.getOperand(outputType.index());
        auto newUsage = analysis.lookupResourceUsage(returnValue);
        auto newLifetime = convertUsageToLifetime(newUsage);
        auto newType =
            rewriter.getType<IREE::Stream::ResourceType>(newLifetime);
        newOutputs.push_back(newType);
      } else {
        newOutputs.push_back(oldType);
      }
    }
    auto newFuncType = rewriter.getFunctionType(newInputs, newOutputs);
    if (op.getFunctionType() != newFuncType) {
      op.setType(newFuncType);
      didChange = true;
    }

    // Blocks and nested operations:
    if (this->applyRegionTransitions(op, rewriter)) didChange = true;

    return success(didChange);
  }
};

// Applies usage analysis results to a generic MLIR op.
// All resource operands and results including those in nested regions will have
// their lifetime specified.
template <typename Op>
struct ApplyGenericOp : public UsageRefinementPattern<Op> {
  using UsageRefinementPattern<Op>::UsageRefinementPattern;
  LogicalResult matchAndRewrite(Op op,
                                PatternRewriter &rewriter) const override {
    bool didChange = this->applyRegionTransitions(op, rewriter);
    rewriter.startRootUpdate(op);
    for (unsigned i = 0; i < op->getNumResults(); ++i) {
      auto result = op->getResult(i);
      if (result.getType().template isa<IREE::Stream::ResourceType>()) {
        if (this->applyResultTransition(op, result, rewriter)) didChange = true;
      }
    }
    if (didChange) {
      rewriter.finalizeRootUpdate(op);
    } else {
      rewriter.cancelRootUpdate(op);
    }
    return success(didChange);
  }
};

// Applies usage analysis results to a stream-dialect streamable op.
// All resource operands and results including those in nested regions will have
// their lifetime specified.
template <typename Op>
struct ApplyStreamableOp : public UsageRefinementPattern<Op> {
  using UsageRefinementPattern<Op>::UsageRefinementPattern;
  LogicalResult matchAndRewrite(Op op,
                                PatternRewriter &rewriter) const override {
    // Walk into nested regions first so we have the final result types returned
    // by the regions.
    bool didChange = this->applyRegionTransitions(op, rewriter);
    auto affinityAttr = getOpAffinity(op);

    rewriter.startRootUpdate(op);

    auto sizeAwareOp =
        dyn_cast<IREE::Util::SizeAwareOpInterface>(op.getOperation());
    for (unsigned i = 0; i < op->getNumResults(); ++i) {
      auto result = op->getResult(i);
      if (!result.getType().template isa<IREE::Stream::ResourceType>()) {
        continue;
      }
      auto resultSize = sizeAwareOp.getResultSize(i);
      if (this->applyResultTransition(result, resultSize, affinityAttr,
                                      rewriter)) {
        didChange = true;
      }
    }

    if (didChange) {
      rewriter.finalizeRootUpdate(op);
    } else {
      rewriter.cancelRootUpdate(op);
    }
    return success(didChange);
  }
};

static void insertUsageRefinementPatterns(MLIRContext *context,
                                          ResourceUsageAnalysis &analysis,
                                          RewritePatternSet &patterns) {
  patterns.insert<ApplyInitializerOp, ApplyFuncOp>(context, analysis);
  patterns.insert<ApplyGenericOp<IREE::Util::DoNotOptimizeOp>,
                  ApplyGenericOp<mlir::arith::SelectOp>,
                  ApplyGenericOp<mlir::func::CallOp>>(context, analysis);
  patterns.insert<ApplyStreamableOp<IREE::Stream::ResourceAllocOp>,
                  ApplyStreamableOp<IREE::Stream::ResourceAllocaOp>,
                  ApplyStreamableOp<IREE::Stream::TensorImportOp>,
                  ApplyStreamableOp<IREE::Stream::TensorExportOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncAllocaOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncConstantOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncSplatOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncCloneOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncSliceOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncFillOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncUpdateOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncCopyOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncTransferOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncLoadOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncStoreOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncDispatchOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncExecuteOp>,
                  ApplyStreamableOp<IREE::Stream::AsyncConcurrentOp>,
                  ApplyStreamableOp<IREE::Stream::YieldOp>>(context, analysis);
  IREE::Stream::AsyncTransferOp::getCanonicalizationPatterns(patterns, context);
}

//===----------------------------------------------------------------------===//
// -iree-stream-refine-usage
//===----------------------------------------------------------------------===//

class RefineUsagePass : public RefineUsageBase<RefineUsagePass> {
 public:
  RefineUsagePass() = default;

  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<mlir::func::FuncDialect>();
    registry.insert<IREE::Stream::StreamDialect>();
    registry.insert<IREE::Util::UtilDialect>();
  }

  void runOnOperation() override {
    auto moduleOp = getOperation();
    if (moduleOp.getBody()->empty()) return;

    // Run analysis on the entire module.
    ResourceUsageAnalysis analysis(moduleOp);
    if (failed(analysis.run())) {
      moduleOp.emitError() << "failed to solve for usage analysis";
      return signalPassFailure();
    }

    // Query and apply analysis results to all resources in the program.
    RewritePatternSet patterns(&getContext());
    insertUsageRefinementPatterns(&getContext(), analysis, patterns);
    FrozenRewritePatternSet frozenPatterns(std::move(patterns));
    if (failed(applyPatternsAndFoldGreedily(moduleOp, frozenPatterns))) {
      return signalPassFailure();
    }
  }
};

}  // namespace

std::unique_ptr<OperationPass<mlir::ModuleOp>> createRefineUsagePass() {
  return std::make_unique<RefineUsagePass>();
}

}  // namespace Stream
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
