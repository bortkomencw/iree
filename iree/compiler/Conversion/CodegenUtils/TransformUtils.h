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
#ifndef IREE_COMPILER_CONVERSION_CODEGENUTILS_TRANSFORMUTILS_H_
#define IREE_COMPILER_CONVERSION_CODEGENUTILS_TRANSFORMUTILS_H_

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "mlir/Conversion/VectorToSCF/VectorToSCF.h"
#include "mlir/Dialect/Linalg/Transforms/Transforms.h"
#include "mlir/Dialect/Vector/VectorOps.h"
#include "mlir/Dialect/Vector/VectorTransforms.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

namespace mlir {

/// Perform folding of chains of AffineMinOp.
struct AffineMinCanonicalizationPattern
    : public mlir::OpRewritePattern<mlir::AffineMinOp> {
  using OpRewritePattern<mlir::AffineMinOp>::OpRewritePattern;

  mlir::LogicalResult matchAndRewrite(
      mlir::AffineMinOp minOp, mlir::PatternRewriter &rewriter) const override;
};

/// Return a fused vector::ContractionOp which represents a patterns such as:
///
/// ```mlir
///    %c0 = vector.constant 0: ...
///    %c = vector.contract %a, %b, %c0: ...
///    %e = add %c, %d: ...
/// ```
///
/// by:
///
/// ```mlir
///    %e = vector.contract %a, %b, %d: ...
/// ```
///
/// Return null if the canonicalization does not apply.
// TODO: This should be a folding of Add into Contract in core but while they
// live in different dialects, it is not possible without unnatural
// dependencies.
vector::ContractionOp canonicalizeContractionAdd(Operation *op);

}  // namespace mlir

#endif  // IREE_COMPILER_CONVERSION_CODEGENUTILS_TRANSFORMUTILS_H_
