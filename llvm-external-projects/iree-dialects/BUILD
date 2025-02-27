load("@llvm-project//mlir:tblgen.bzl", "gentbl_cc_library", "gentbl_filegroup", "td_library")

package(
    default_visibility = ["//visibility:public"],
    features = ["layering_check"],
    licenses = ["notice"],
)

exports_files(glob(["include/iree-dialects/Dialect/Input/*.td"]))

exports_files(glob(["python/*.cpp"]))

################################################################################
# Package-by-package filegroup exports.
# Some tools can make use of such non-nested groups.
################################################################################

filegroup(
    name = "python_dialects",
    srcs = glob(["python/iree/compiler/dialects/**/*.py"]) + [
        ":python/iree/compiler/dialects/_iree_ops_gen.py",
        ":python/iree/compiler/dialects/_iree_pydm_ops_gen.py",
    ],
)

################################################################################
# Tablegen exports
################################################################################

filegroup(
    name = "TdFilegroup",
    srcs = glob([
        "include/iree-dialects/Dialect/Input/*.td",
        "include/iree-dialects/Dialect/LinalgExt/IR/*.td",
        "include/iree-dialects/Dialect/LinalgExt/Passes/*.td",
        "include/iree-dialects/Dialect/PyDM/IR/*.td",
        "include/iree-dialects/Dialect/PyDM/Transforms/*.td",
    ]),
)

td_library(
    name = "TdFiles",
    srcs = glob([
        "include/iree-dialects/Dialect/Input/*.td",
        "include/iree-dialects/Dialect/LinalgExt/IR/*.td",
        "include/iree-dialects/Dialect/LinalgExt/Passes/*.td",
        "include/iree-dialects/Dialect/LinalgTransform/*.td",
        "include/iree-dialects/Dialect/PyDM/IR/*.td",
        "python/iree/compiler/dialects/*.td",
    ]) + [
        "@llvm-project//mlir:include/mlir/Bindings/Python/Attributes.td",
    ],
    includes = ["include"],
    deps = [
        "@llvm-project//mlir:InferTypeOpInterfaceTdFiles",
        "@llvm-project//mlir:OpBaseTdFiles",
        "@llvm-project//mlir:PDLDialectTdFiles",
        "@llvm-project//mlir:SideEffectTdFiles",
    ],
)

################################################################################
# IREEInput dialect
################################################################################

gentbl_cc_library(
    name = "IREEInputIncGen",
    strip_include_prefix = "include",
    tbl_outs = [
        (
            ["-gen-dialect-decls"],
            "include/iree-dialects/Dialect/Input/InputDialect.h.inc",
        ),
        (
            ["-gen-dialect-defs"],
            "include/iree-dialects/Dialect/Input/InputDialect.cpp.inc",
        ),
        (
            ["-gen-op-decls"],
            "include/iree-dialects/Dialect/Input/InputOps.h.inc",
        ),
        (
            ["-gen-op-defs"],
            "include/iree-dialects/Dialect/Input/InputOps.cpp.inc",
        ),
        (
            ["-gen-typedef-decls"],
            "include/iree-dialects/Dialect/Input/InputTypes.h.inc",
        ),
        (
            ["-gen-typedef-defs"],
            "include/iree-dialects/Dialect/Input/InputTypes.cpp.inc",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "include/iree-dialects/Dialect/Input/InputOps.td",
    deps = [":TdFiles"],
)

cc_library(
    name = "IREEInputDialect",
    srcs = glob([
        "lib/Dialect/Input/*.cpp",
    ]),
    hdrs = glob(["include/iree-dialects/Dialect/Input/*.h"]),
    includes = ["include"],
    deps = [
        ":IREEInputIncGen",
        "@llvm-project//llvm:Support",
        "@llvm-project//mlir:ArithmeticDialect",
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:InferTypeOpInterface",
        "@llvm-project//mlir:Support",
    ],
)

gentbl_filegroup(
    name = "IREEInputDialectPyGen",
    tbl_outs = [
        (
            [
                "-gen-python-op-bindings",
                "-bind-dialect=iree",
            ],
            "python/iree/compiler/dialects/_iree_ops_gen.py",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "python/iree/compiler/dialects/IreeInputBinding.td",
    deps = [
        ":TdFiles",
    ],
)

################################################################################
# IREELinalgExt Dialect
################################################################################

cc_library(
    name = "IREEDialectsTransforms",
    srcs = glob([
        "lib/Transforms/*.cpp",
    ]),
    hdrs = glob([
        "include/iree-dialects/Transforms/*.h",
    ]),
    includes = ["include"],
    deps = [
        "@llvm-project//llvm:Support",
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:Rewrite",
        "@llvm-project//mlir:Transforms",
    ],
)

gentbl_cc_library(
    name = "IREELinalgExtIncGen",
    strip_include_prefix = "include",
    tbl_outs = [
        (
            [
                "-dialect=iree_linalg_ext",
                "-gen-dialect-decls",
            ],
            "include/iree-dialects/Dialect/LinalgExt/IR/LinalgExtDialect.h.inc",
        ),
        (
            [
                "-dialect=iree_linalg_ext",
                "-gen-dialect-defs",
            ],
            "include/iree-dialects/Dialect/LinalgExt/IR/LinalgExtDialect.cpp.inc",
        ),
        (
            ["-gen-op-decls"],
            "include/iree-dialects/Dialect/LinalgExt/IR/LinalgExtOps.h.inc",
        ),
        (
            ["-gen-op-defs"],
            "include/iree-dialects/Dialect/LinalgExt/IR/LinalgExtOps.cpp.inc",
        ),
        (
            ["-gen-typedef-decls"],
            "include/iree-dialects/Dialect/LinalgExt/IR/LinalgExtTypes.h.inc",
        ),
        (
            ["-gen-typedef-defs"],
            "include/iree-dialects/Dialect/LinalgExt/IR/LinalgExtTypes.cpp.inc",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "include/iree-dialects/Dialect/LinalgExt/IR/LinalgExtOps.td",
    deps = [
        ":TdFiles",
        "@llvm-project//mlir:CallInterfacesTdFiles",
        "@llvm-project//mlir:ControlFlowInterfacesTdFiles",
        "@llvm-project//mlir:InferTypeOpInterfaceTdFiles",
        "@llvm-project//mlir:TilingInterfaceTdFiles",
        "@llvm-project//mlir:ViewLikeInterfaceTdFiles",
    ],
)

gentbl_cc_library(
    name = "IREELinalgExtInterfacesIncGen",
    strip_include_prefix = "include",
    tbl_outs = [
        (
            ["-gen-op-interface-decls"],
            "include/iree-dialects/Dialect/LinalgExt/IR/LinalgExtOpInterfaces.h.inc",
        ),
        (
            ["-gen-op-interface-defs"],
            "include/iree-dialects/Dialect/LinalgExt/IR/LinalgExtOpInterfaces.cpp.inc",
        ),
        (
            ["-gen-type-interface-decls"],
            "include/iree-dialects/Dialect/LinalgExt/IR/LinalgExtTypeInterfaces.h.inc",
        ),
        (
            ["-gen-type-interface-defs"],
            "include/iree-dialects/Dialect/LinalgExt/IR/LinalgExtTypeInterfaces.cpp.inc",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "include/iree-dialects/Dialect/LinalgExt/IR/LinalgExtInterfaces.td",
    deps = [
        ":TdFiles",
    ],
)

gentbl_cc_library(
    name = "IREELinalgExtTiledOpInterfacesIncGen",
    strip_include_prefix = "include",
    tbl_outs = [
        (
            ["-gen-op-interface-decls"],
            "include/iree-dialects/Dialect/LinalgExt/IR/TiledOpInterface.h.inc",
        ),
        (
            ["-gen-op-interface-defs"],
            "include/iree-dialects/Dialect/LinalgExt/IR/TiledOpInterface.cpp.inc",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "include/iree-dialects/Dialect/LinalgExt/IR/TiledOpInterface.td",
    deps = [
        ":TdFiles",
    ],
)

gentbl_cc_library(
    name = "IREELinalgExtPassIncGen",
    strip_include_prefix = "include",
    tbl_outs = [
        (
            ["-gen-pass-decls"],
            "include/iree-dialects/Dialect/LinalgExt/Passes/Passes.h.inc",
        ),
        (
            ["-gen-pass-capi-header"],
            "include/iree-dialects/Dialect/LinalgExt/Passes/Passes.capi.h.inc",
        ),
        (
            ["-gen-pass-capi-impl"],
            "include/iree-dialects/Dialect/LinalgExt/Passes/Passes.capi.cpp.inc",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "include/iree-dialects/Dialect/LinalgExt/Passes/Passes.td",
    deps = [
        ":TdFiles",
        "@llvm-project//mlir:PassBaseTdFiles",
    ],
)

cc_library(
    name = "IREELinalgExtDialect",
    srcs = glob([
        "lib/Dialect/LinalgExt/IR/*.cpp",
    ]),
    hdrs = glob([
        "include/iree-dialects/Dialect/LinalgExt/IR/*.h",
    ]),
    includes = ["include"],
    deps = [
        ":IREELinalgExtIncGen",
        ":IREELinalgExtInterfacesIncGen",
        ":IREELinalgExtPassIncGen",
        ":IREELinalgExtTiledOpInterfacesIncGen",
        "@llvm-project//llvm:Support",
        "@llvm-project//mlir:Affine",
        "@llvm-project//mlir:ArithmeticUtils",
        "@llvm-project//mlir:ControlFlowInterfaces",
        "@llvm-project//mlir:DialectUtils",
        "@llvm-project//mlir:FuncDialect",
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:InferTypeOpInterface",
        "@llvm-project//mlir:LinalgOps",
        "@llvm-project//mlir:MathDialect",
        "@llvm-project//mlir:MemRefDialect",
        "@llvm-project//mlir:Pass",
        "@llvm-project//mlir:SCFDialect",
        "@llvm-project//mlir:SideEffectInterfaces",
        "@llvm-project//mlir:Support",
        "@llvm-project//mlir:TensorDialect",
        "@llvm-project//mlir:TensorUtils",
        "@llvm-project//mlir:ViewLikeInterface",
    ],
)

cc_library(
    name = "IREELinalgExtPasses",
    srcs = glob([
        "lib/Dialect/LinalgExt/Passes/*.cpp",
    ]),
    hdrs = glob([
        "include/iree-dialects/Dialect/LinalgExt/Passes/*.h",
    ]),
    deps = [
        ":IREEInputDialect",
        ":IREELinalgExtDialect",
        ":IREELinalgExtPassIncGen",
        "@llvm-project//llvm:Support",
        "@llvm-project//mlir:Affine",
        "@llvm-project//mlir:ArithmeticDialect",
        "@llvm-project//mlir:DialectUtils",
        "@llvm-project//mlir:FuncDialect",
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:LinalgOps",
        "@llvm-project//mlir:LinalgTransforms",
        "@llvm-project//mlir:MathDialect",
        "@llvm-project//mlir:MemRefDialect",
        "@llvm-project//mlir:Parser",
        "@llvm-project//mlir:Pass",
        "@llvm-project//mlir:SCFDialect",
        "@llvm-project//mlir:Support",
        "@llvm-project//mlir:TensorDialect",
        "@llvm-project//mlir:TensorUtils",
        "@llvm-project//mlir:TransformUtils",
        "@llvm-project//mlir:Transforms",
    ],
)

cc_library(
    name = "IREELinalgExtTransforms",
    srcs = glob([
        "lib/Dialect/LinalgExt/Transforms/*.cpp",
    ]),
    hdrs = glob([
        "include/iree-dialects/Dialect/LinalgExt/Transforms/*.h",
    ]) + ["include/iree-dialects/Dialect/LinalgExt/LinalgExtBufferization.h"],
    deps = [
        ":IREEInputDialect",
        ":IREELinalgExtDialect",
        ":IREELinalgExtPassIncGen",
        ":IREELinalgExtPasses",
        "@llvm-project//llvm:Support",
        "@llvm-project//mlir:Affine",
        "@llvm-project//mlir:ArithmeticDialect",
        "@llvm-project//mlir:Async",
        "@llvm-project//mlir:BufferizationDialect",
        "@llvm-project//mlir:DialectUtils",
        "@llvm-project//mlir:FuncDialect",
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:LinalgOps",
        "@llvm-project//mlir:LinalgStructuredOpsIncGen",
        "@llvm-project//mlir:LinalgTransforms",
        "@llvm-project//mlir:MathDialect",
        "@llvm-project//mlir:MemRefDialect",
        "@llvm-project//mlir:Parser",
        "@llvm-project//mlir:Pass",
        "@llvm-project//mlir:SCFDialect",
        "@llvm-project//mlir:Support",
        "@llvm-project//mlir:TensorDialect",
        "@llvm-project//mlir:TensorUtils",
        "@llvm-project//mlir:TilingInterface",
        "@llvm-project//mlir:TransformUtils",
        "@llvm-project//mlir:Transforms",
    ],
)

################################################################################
# IREEPyDM Dialect
################################################################################

gentbl_cc_library(
    name = "IREEPyDMOpsIncGen",
    strip_include_prefix = "include",
    tbl_outs = [
        (
            ["-gen-dialect-decls"],
            "include/iree-dialects/Dialect/PyDM/IR/PyDMDialect.h.inc",
        ),
        (
            ["-gen-dialect-defs"],
            "include/iree-dialects/Dialect/PyDM/IR/PyDMDialect.cpp.inc",
        ),
        (
            ["-gen-op-decls"],
            "include/iree-dialects/Dialect/PyDM/IR/PyDMOps.h.inc",
        ),
        (
            ["-gen-op-defs"],
            "include/iree-dialects/Dialect/PyDM/IR/PyDMOps.cpp.inc",
        ),
        (
            ["-gen-typedef-decls"],
            "include/iree-dialects/Dialect/PyDM/IR/PyDMTypes.h.inc",
        ),
        (
            ["-gen-typedef-defs"],
            "include/iree-dialects/Dialect/PyDM/IR/PyDMTypes.cpp.inc",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "include/iree-dialects/Dialect/PyDM/IR/PyDMOps.td",
    deps = [
        ":TdFiles",
        "@llvm-project//mlir:CallInterfacesTdFiles",
        "@llvm-project//mlir:ControlFlowInterfacesTdFiles",
        "@llvm-project//mlir:FunctionInterfacesTdFiles",
    ],
)

gentbl_cc_library(
    name = "IREEPyDMInterfacesIncGen",
    strip_include_prefix = "include",
    tbl_outs = [
        (
            ["-gen-op-interface-decls"],
            "include/iree-dialects/Dialect/PyDM/IR/PyDMOpInterfaces.h.inc",
        ),
        (
            ["-gen-op-interface-defs"],
            "include/iree-dialects/Dialect/PyDM/IR/PyDMOpInterfaces.cpp.inc",
        ),
        (
            ["-gen-type-interface-decls"],
            "include/iree-dialects/Dialect/PyDM/IR/PyDMTypeInterfaces.h.inc",
        ),
        (
            ["-gen-type-interface-defs"],
            "include/iree-dialects/Dialect/PyDM/IR/PyDMTypeInterfaces.cpp.inc",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "include/iree-dialects/Dialect/PyDM/IR/PyDMInterfaces.td",
    deps = [
        ":TdFiles",
    ],
)

gentbl_cc_library(
    name = "IREEPyDMTransformsIncGen",
    strip_include_prefix = "include",
    tbl_outs = [
        (
            ["-gen-pass-decls"],
            "include/iree-dialects/Dialect/PyDM/Transforms/Passes.h.inc",
        ),
        (
            ["-gen-pass-capi-header"],
            "include/iree-dialects/Dialect/PyDM/Transforms/Passes.capi.h.inc",
        ),
        (
            ["-gen-pass-capi-impl"],
            "include/iree-dialects/Dialect/PyDM/Transforms/Passes.capi.cpp.inc",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "include/iree-dialects/Dialect/PyDM/Transforms/Passes.td",
    deps = [
        ":TdFiles",
        "@llvm-project//mlir:PassBaseTdFiles",
    ],
)

gentbl_filegroup(
    name = "IREEPyDMDialectPyGen",
    tbl_outs = [
        (
            [
                "-gen-python-op-bindings",
                "-bind-dialect=iree_pydm",
            ],
            "python/iree/compiler/dialects/_iree_pydm_ops_gen.py",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "python/iree/compiler/dialects/IreePyDmBinding.td",
    deps = [
        ":TdFiles",
        "@llvm-project//mlir:CallInterfacesTdFiles",
        "@llvm-project//mlir:ControlFlowInterfacesTdFiles",
        "@llvm-project//mlir:FunctionInterfacesTdFiles",
    ],
)

cc_library(
    name = "IREEPyDMDialect",
    srcs = glob([
        "lib/Dialect/PyDM/IR/*.cpp",
    ]),
    hdrs = glob([
        "include/iree-dialects/Dialect/PyDM/IR/*.h",
    ]),
    includes = ["include"],
    deps = [
        ":IREEPyDMInterfacesIncGen",
        ":IREEPyDMOpsIncGen",
        ":IREEPyDMTransformsIncGen",
        "@llvm-project//llvm:Support",
        "@llvm-project//mlir:CallOpInterfaces",
        "@llvm-project//mlir:ControlFlowInterfaces",
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:Pass",
        "@llvm-project//mlir:Support",
    ],
)

cc_library(
    name = "IREEPyDMTransforms",
    srcs = glob([
        "lib/Dialect/PyDM/Transforms/*.cpp",
        "lib/Dialect/PyDM/Transforms/Optimize/*.cpp",
        "lib/Dialect/PyDM/Transforms/RTL/*.cpp",
        "lib/Dialect/PyDM/Transforms/ToIREE/*.cpp",
        "lib/Dialect/PyDM/Utils/*.cpp",
    ]),
    hdrs = glob([
        "include/iree-dialects/Dialect/PyDM/Transforms/*.h",
        "include/iree-dialects/Dialect/PyDM/Transforms/**/*.h",
        "include/iree-dialects/Dialect/PyDM/Utils/*.h",
        "lib/Dialect/PyDM/Transforms/*.h",
    ]),
    deps = [
        ":IREEInputDialect",
        ":IREEPyDMDialect",
        ":IREEPyDMTransformsIncGen",
        "@llvm-project//llvm:Support",
        "@llvm-project//mlir:ArithmeticDialect",
        "@llvm-project//mlir:ControlFlowInterfaces",
        "@llvm-project//mlir:ControlFlowOps",
        "@llvm-project//mlir:FuncDialect",
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:MathDialect",
        "@llvm-project//mlir:Parser",
        "@llvm-project//mlir:Pass",
        "@llvm-project//mlir:Support",
        "@llvm-project//mlir:TransformUtils",
        "@llvm-project//mlir:Transforms",
    ],
)

################################################################################
# IREELinalgTransform Dialect
################################################################################

gentbl_cc_library(
    name = "IREELinalgTransformIncGen",
    strip_include_prefix = "include",
    tbl_outs = [
        (
            [
                "-dialect=iree_linalg_transform",
                "-gen-dialect-decls",
            ],
            "include/iree-dialects/Dialect/LinalgTransform/LinalgTransformDialect.h.inc",
        ),
        (
            [
                "-dialect=iree_linalg_transform",
                "-gen-dialect-defs",
            ],
            "include/iree-dialects/Dialect/LinalgTransform/LinalgTransformDialect.cpp.inc",
        ),
        (
            ["-gen-op-decls"],
            "include/iree-dialects/Dialect/LinalgTransform/LinalgTransformOps.h.inc",
        ),
        (
            ["-gen-op-defs"],
            "include/iree-dialects/Dialect/LinalgTransform/LinalgTransformOps.cpp.inc",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "include/iree-dialects/Dialect/LinalgTransform/LinalgTransformOps.td",
    deps = [
        ":TdFiles",
        "@llvm-project//mlir:ControlFlowInterfacesTdFiles",
    ],
)

gentbl_cc_library(
    name = "IREELinalgTransformInterfacesIncGen",
    strip_include_prefix = "include",
    tbl_outs = [
        (
            ["-gen-op-interface-decls"],
            "include/iree-dialects/Dialect/LinalgTransform/TransformOpInterface.h.inc",
        ),
        (
            ["-gen-op-interface-defs"],
            "include/iree-dialects/Dialect/LinalgTransform/TransformOpInterface.cpp.inc",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "include/iree-dialects/Dialect/LinalgTransform/TransformOpInterface.td",
    deps = [
        ":TdFiles",
    ],
)

cc_library(
    name = "IREELinalgTransformDialect",
    srcs = glob([
        "lib/Dialect/LinalgTransform/IR/*.cpp",
        "lib/Dialect/LinalgTransform/IR/*.h",
    ]),
    hdrs = glob([
        "include/iree-dialects/Dialect/LinalgTransform/*.h",
    ]),
    includes = ["include"],
    deps = [
        ":IREEDialectsTransforms",
        ":IREELinalgExtDialect",
        ":IREELinalgExtTransforms",
        ":IREELinalgTransformIncGen",
        ":IREELinalgTransformInterfacesIncGen",
        "@llvm-project//llvm:Support",
        # Dialects
        "@llvm-project//mlir:Async",
        "@llvm-project//mlir:ArithmeticDialect",
        "@llvm-project//mlir:BufferizationDialect",
        "@llvm-project//mlir:BufferizationTransforms",
        "@llvm-project//mlir:FuncDialect",
        "@llvm-project//mlir:LinalgOps",
        "@llvm-project//mlir:LLVMDialect",
        "@llvm-project//mlir:PDLDialect",
        "@llvm-project//mlir:SCFDialect",
        "@llvm-project//mlir:TensorDialect",
        # IR
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:Parser",
        "@llvm-project//mlir:Pass",
        "@llvm-project//mlir:Rewrite",
        # Interfaces
        "@llvm-project//mlir:ControlFlowInterfaces",
        "@llvm-project//mlir:LinalgInterfaces",
        # Transforms
        "@llvm-project//mlir:AsyncTransforms",
        "@llvm-project//mlir:LinalgTransforms",
        "@llvm-project//mlir:AffineToStandard",
        "@llvm-project//mlir:ModuleBufferization",
        "@llvm-project//mlir:SCFTransforms",
        "@llvm-project//mlir:Transforms",
        "@llvm-project//mlir:ReconcileUnrealizedCasts",
        # Utils
        "@llvm-project//mlir:ArithmeticUtils",
        "@llvm-project//mlir:DialectUtils",
        # Conversions
        "@llvm-project//mlir:AsyncToLLVM",
        "@llvm-project//mlir:FuncToLLVM",
        "@llvm-project//mlir:LinalgToLLVM",
        "@llvm-project//mlir:LinalgToStandard",
        "@llvm-project//mlir:MathToLLVM",
        "@llvm-project//mlir:MemRefToLLVM",
        "@llvm-project//mlir:SCFToControlFlow",
        "@llvm-project//mlir:VectorToLLVM",
    ],
)

cc_library(
    name = "IREELinalgTransformDialectTransforms",
    srcs = glob([
        "lib/Dialect/LinalgTransform/Transforms/*.cpp",
    ]),
    hdrs = [
        "include/iree-dialects/Dialect/LinalgTransform/TrackingCSE.h",
        "include/iree-dialects/Dialect/LinalgTransform/TrackingListener.h",
        "include/iree-dialects/Dialect/LinalgTransform/TrackingRewriteDriver.h",
    ],
    deps = [
        ":IREEDialectsTransforms",
        ":IREELinalgExtDialect",
        ":IREELinalgTransformDialect",
        "@llvm-project//llvm:Support",
        "@llvm-project//mlir:Affine",
        "@llvm-project//mlir:AffineUtils",
        "@llvm-project//mlir:ArithmeticTransforms",
        "@llvm-project//mlir:BufferizationDialect",
        "@llvm-project//mlir:DialectUtils",
        "@llvm-project//mlir:FuncDialect",
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:LLVMDialect",
        "@llvm-project//mlir:LinalgOps",
        "@llvm-project//mlir:LinalgTransforms",
        "@llvm-project//mlir:MathDialect",
        "@llvm-project//mlir:MemRefDialect",
        "@llvm-project//mlir:ModuleBufferization",
        "@llvm-project//mlir:PDLDialect",
        "@llvm-project//mlir:PDLInterpDialect",
        "@llvm-project//mlir:Parser",
        "@llvm-project//mlir:Pass",
        "@llvm-project//mlir:Rewrite",
        "@llvm-project//mlir:SCFDialect",
        "@llvm-project//mlir:SCFTransforms",
        "@llvm-project//mlir:SCFUtils",
        "@llvm-project//mlir:Support",
        "@llvm-project//mlir:TensorDialect",
        "@llvm-project//mlir:TensorTransforms",
        "@llvm-project//mlir:TensorUtils",
        "@llvm-project//mlir:TransformUtils",
        "@llvm-project//mlir:Transforms",
        "@llvm-project//mlir:VectorOps",
        "@llvm-project//mlir:VectorToLLVM",
        "@llvm-project//mlir:VectorTransforms",
    ],
)

################################################################################
# CAPI
################################################################################

cc_library(
    name = "CAPI",
    srcs = glob(["lib/CAPI/*.cpp"]),
    hdrs = glob(["include/iree-dialects-c/*.h"]),
    includes = ["include"],
    deps = [
        ":IREEInputDialect",
        ":IREELinalgExtDialect",
        ":IREELinalgTransformDialect",
        ":IREEPyDMDialect",
        ":IREEPyDMTransforms",
        "@llvm-project//mlir:CAPIIR",
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:Support",
    ],
)

################################################################################
# Test lib
################################################################################

cc_library(
    name = "IREEDialectsTest",
    srcs = glob([
        "test/lib/**/*.cpp",
    ]),
    deps = [
        ":IREEDialectsTransforms",
        ":IREELinalgExtDialect",
        ":IREELinalgTransformDialect",
        ":IREELinalgTransformDialectTransforms",
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:Pass",
        "@llvm-project//mlir:Rewrite",
        "@llvm-project//mlir:Transforms",
    ],
)

################################################################################
# Tools
################################################################################

cc_binary(
    name = "iree-dialects-opt",
    srcs = [
        "tools/iree-dialects-opt/iree-dialects-opt.cpp",
    ],
    tags = ["hostonly"],
    deps = [
        "IREELinalgExtTransforms",
        ":IREEDialectsTest",
        ":IREEInputDialect",
        ":IREELinalgExtDialect",
        ":IREELinalgExtPasses",
        ":IREELinalgTransformDialect",
        ":IREELinalgTransformDialectTransforms",
        ":IREEPyDMDialect",
        ":IREEPyDMTransforms",
        "@llvm-project//llvm:Support",
        "@llvm-project//mlir:ArithmeticDialect",
        "@llvm-project//mlir:Async",
        "@llvm-project//mlir:ControlFlowOps",
        "@llvm-project//mlir:FuncDialect",
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:LinalgOps",
        "@llvm-project//mlir:MemRefDialect",
        "@llvm-project//mlir:MlirOptLib",
        "@llvm-project//mlir:PDLDialect",
        "@llvm-project//mlir:PDLInterpDialect",
        "@llvm-project//mlir:SCFDialect",
        "@llvm-project//mlir:Support",
        "@llvm-project//mlir:TensorDialect",
        "@llvm-project//mlir:Transforms",
    ],
)
