// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/modules/check/module.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "iree/base/api.h"
#include "iree/base/internal/math.h"
#include "iree/base/status_cc.h"
#include "iree/hal/api.h"
#include "iree/modules/hal/module.h"
#include "iree/testing/gtest.h"
#include "iree/vm/native_module_cc.h"
#include "iree/vm/ref_cc.h"

//===----------------------------------------------------------------------===//
// VM module interface implementation
//===----------------------------------------------------------------------===//

namespace iree {
namespace {

using ::testing::Each;
using ::testing::Not;

template <typename T>
iree::span<const T> ToSpan(iree_byte_span_t bytes) {
  return iree::span<const T>(reinterpret_cast<T*>(bytes.data),
                             bytes.data_length / sizeof(T));
}

StatusOr<std::string> BufferViewToString(iree_hal_buffer_view_t* buffer_view) {
  std::string result_str(4096, '\0');
  iree_status_t status;
  do {
    iree_host_size_t actual_length = 0;
    status = iree_hal_buffer_view_format(
        buffer_view, /*max_element_count=*/1024, result_str.size() + 1,
        &result_str[0], &actual_length);
    result_str.resize(actual_length);
  } while (iree_status_is_out_of_range(status));
  IREE_RETURN_IF_ERROR(std::move(status));
  return std::move(result_str);
}

template <typename T>
Status ExpectAllTrue(iree_byte_span_t bytes) {
  EXPECT_THAT(ToSpan<T>(bytes), Each(Not(T(0))));
  return OkStatus();
}

bool EqByteSpan(iree_byte_span_t lhs_bytes, iree_byte_span_t rhs_bytes) {
  return lhs_bytes.data_length == rhs_bytes.data_length &&
         memcmp(lhs_bytes.data, rhs_bytes.data, lhs_bytes.data_length) == 0;
}

static constexpr float kF32PrecisionThreshold = 0.0001f;

template <typename T>
bool AlmostEqByteSpan(iree_byte_span_t lhs_bytes, iree_byte_span_t rhs_bytes) {
  auto lhs_span = ToSpan<T>(lhs_bytes);
  auto rhs_span = ToSpan<T>(rhs_bytes);
  assert(lhs_span.size() == rhs_span.size());
  for (int i = 0; i < lhs_span.size(); ++i) {
    if (fabs(lhs_span[i] - rhs_span[i]) > kF32PrecisionThreshold) {
      return false;
    }
  }
  return true;
}

static constexpr float kF16PrecisionThreshold = 0.001f;

bool AlmostEqByteSpanF16(iree_byte_span_t lhs_bytes,
                         iree_byte_span_t rhs_bytes) {
  auto lhs_span = ToSpan<uint16_t>(lhs_bytes);
  auto rhs_span = ToSpan<uint16_t>(rhs_bytes);
  assert(lhs_span.size() == rhs_span.size());
  for (int i = 0; i < lhs_span.size(); ++i) {
    if (fabs(iree_math_f16_to_f32(lhs_span[i]) -
             iree_math_f16_to_f32(rhs_span[i])) > kF16PrecisionThreshold) {
      return false;
    }
  }
  return true;
}

StatusOr<bool> AlmostEqByteSpan(iree_byte_span_t lhs_bytes,
                                iree_byte_span_t rhs_bytes,
                                iree_hal_element_type_t element_type) {
  switch (element_type) {
    case IREE_HAL_ELEMENT_TYPE_FLOAT_32:
      return AlmostEqByteSpan<float>(lhs_bytes, rhs_bytes);
    case IREE_HAL_ELEMENT_TYPE_FLOAT_64:
      return AlmostEqByteSpan<double>(lhs_bytes, rhs_bytes);
    case IREE_HAL_ELEMENT_TYPE_FLOAT_16:
      return AlmostEqByteSpanF16(lhs_bytes, rhs_bytes);
    default:
      // TODO(gcmn): Consider supporting fuzzy matching for quantized integers.
      break;
  }
  char element_type_str[16];
  IREE_RETURN_IF_ERROR(iree_hal_format_element_type(
      element_type, sizeof(element_type_str), element_type_str, nullptr));
  return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                          "unsupported element type %s", element_type_str);
}

Status ExpectAllTrue(iree_byte_span_t bytes,
                     iree_hal_element_type_t element_type) {
  switch (element_type) {
    case IREE_HAL_ELEMENT_TYPE_INT_8:
    case IREE_HAL_ELEMENT_TYPE_SINT_8:
      return ExpectAllTrue<int8_t>(bytes);
    case IREE_HAL_ELEMENT_TYPE_UINT_8:
      return ExpectAllTrue<uint8_t>(bytes);
    case IREE_HAL_ELEMENT_TYPE_INT_16:
    case IREE_HAL_ELEMENT_TYPE_SINT_16:
      return ExpectAllTrue<int16_t>(bytes);
    case IREE_HAL_ELEMENT_TYPE_UINT_16:
      return ExpectAllTrue<uint16_t>(bytes);
    case IREE_HAL_ELEMENT_TYPE_INT_32:
    case IREE_HAL_ELEMENT_TYPE_SINT_32:
      return ExpectAllTrue<int32_t>(bytes);
    case IREE_HAL_ELEMENT_TYPE_UINT_32:
      return ExpectAllTrue<uint32_t>(bytes);
    case IREE_HAL_ELEMENT_TYPE_INT_64:
    case IREE_HAL_ELEMENT_TYPE_SINT_64:
      return ExpectAllTrue<int64_t>(bytes);
    case IREE_HAL_ELEMENT_TYPE_UINT_64:
      return ExpectAllTrue<uint64_t>(bytes);
    case IREE_HAL_ELEMENT_TYPE_FLOAT_32:
      return ExpectAllTrue<float>(bytes);
    case IREE_HAL_ELEMENT_TYPE_FLOAT_64:
      return ExpectAllTrue<double>(bytes);
    default:
      break;
  }
  char element_type_str[16];
  IREE_RETURN_IF_ERROR(iree_hal_format_element_type(
      element_type, sizeof(element_type_str), element_type_str, nullptr));
  return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                          "unsupported element type %s", element_type_str);
}

// Per-context module state.
// This can contain "globals" and other arbitrary state.
//
// Thread-compatible; the runtime will not issue multiple calls at the same
// time using the same state. If the implementation uses external threads then
// it must synchronize itself.
class CheckModuleState final {
 public:
  explicit CheckModuleState(iree_allocator_t allocator)
      : allocator_(allocator) {}
  ~CheckModuleState() = default;

  Status ExpectTrue(int32_t operand) {
    EXPECT_TRUE(operand) << "Expected " << operand << " to be nonzero.";
    return OkStatus();
  }

  Status ExpectFalse(int32_t operand) {
    EXPECT_FALSE(operand) << "Expected " << operand << " to be zero.";
    return OkStatus();
  }

  Status ExpectAllTrue(vm::ref<iree_hal_buffer_view_t> operand) {
    auto* view = operand.get();
    iree_hal_element_type_t element_type =
        iree_hal_buffer_view_element_type(view);
    iree_hal_buffer_t* buf = iree_hal_buffer_view_buffer(view);
    iree_device_size_t size = iree_hal_buffer_view_byte_length(view);
    iree_hal_buffer_mapping_t mapped_memory;
    IREE_RETURN_IF_ERROR(
        iree_hal_buffer_map_range(buf, IREE_HAL_MEMORY_ACCESS_READ,
                                  /*byte_offset=*/0, size, &mapped_memory));
    IREE_RETURN_IF_ERROR(
        ::iree::ExpectAllTrue(mapped_memory.contents, element_type));
    iree_hal_buffer_unmap_range(&mapped_memory);
    return OkStatus();
  }

  Status ExpectEq(vm::ref<iree_hal_buffer_view_t> lhs_ref,
                  vm::ref<iree_hal_buffer_view_t> rhs_ref) {
    auto* lhs = lhs_ref.get();
    auto* rhs = rhs_ref.get();

    iree_device_size_t lhs_size = iree_hal_buffer_view_byte_length(lhs);
    size_t lhs_rank = iree_hal_buffer_view_shape_rank(lhs);
    std::vector<iree_hal_dim_t> lhs_shape(lhs_rank);
    if (lhs_rank > 0) {
      IREE_RETURN_IF_ERROR(
          iree_hal_buffer_view_shape(lhs, lhs_rank, lhs_shape.data(), nullptr));
    }

    iree_device_size_t rhs_size = iree_hal_buffer_view_byte_length(rhs);
    size_t rhs_rank = iree_hal_buffer_view_shape_rank(rhs);
    std::vector<iree_hal_dim_t> rhs_shape(rhs_rank);
    if (rhs_rank > 0) {
      IREE_RETURN_IF_ERROR(
          iree_hal_buffer_view_shape(rhs, rhs_rank, rhs_shape.data(), nullptr));
    }

    iree_hal_element_type_t lhs_element_type =
        iree_hal_buffer_view_element_type(lhs);
    iree_hal_element_type_t rhs_element_type =
        iree_hal_buffer_view_element_type(rhs);

    iree_hal_buffer_t* lhs_buf = iree_hal_buffer_view_buffer(lhs);
    iree_hal_buffer_mapping_t lhs_mapped_memory;
    IREE_RETURN_IF_ERROR(iree_hal_buffer_map_range(
        lhs_buf, IREE_HAL_MEMORY_ACCESS_READ,
        /*byte_offset=*/0, lhs_size, &lhs_mapped_memory));
    iree_hal_buffer_t* rhs_buf = iree_hal_buffer_view_buffer(rhs);
    iree_hal_buffer_mapping_t rhs_mapped_memory;
    IREE_RETURN_IF_ERROR(iree_hal_buffer_map_range(
        rhs_buf, IREE_HAL_MEMORY_ACCESS_READ,
        /*byte_offset=*/0, rhs_size, &rhs_mapped_memory));

    bool element_types_eq = lhs_element_type == rhs_element_type;
    bool shape_eq = lhs_shape == rhs_shape;
    bool contents_eq =
        EqByteSpan(lhs_mapped_memory.contents, rhs_mapped_memory.contents);
    iree_hal_buffer_unmap_range(&lhs_mapped_memory);
    iree_hal_buffer_unmap_range(&rhs_mapped_memory);

    if (!element_types_eq || !shape_eq || !contents_eq) {
      std::ostringstream os;
      os << "Expected equality of these values.";
      if (!element_types_eq) {
        os << " Element types do not match.";
      }
      if (!shape_eq) {
        os << " Shapes do not match.";
      }
      if (!contents_eq) {
        os << " Contents does not match.";
      }
      // TODO(b/146898896): Propagate original variable names.
      os << "\n"
            "  lhs:\n"
            "    ";
      IREE_ASSIGN_OR_RETURN(auto lhs_str, BufferViewToString(lhs));
      os << lhs_str;

      os << "\n"
            "  rhs:\n"
            "    ";
      IREE_ASSIGN_OR_RETURN(auto rhs_str, BufferViewToString(rhs));
      os << rhs_str;

      // TODO(b/146898896): Use ADD_FAILURE_AT to propagate source location.
      ADD_FAILURE() << os.str();
    }

    return OkStatus();
  }

  Status ExpectAlmostEq(vm::ref<iree_hal_buffer_view_t> lhs_ref,
                        vm::ref<iree_hal_buffer_view_t> rhs_ref) {
    auto* lhs = lhs_ref.get();
    auto* rhs = rhs_ref.get();

    iree_device_size_t lhs_size = iree_hal_buffer_view_byte_length(lhs);
    size_t lhs_rank = iree_hal_buffer_view_shape_rank(lhs);
    std::vector<iree_hal_dim_t> lhs_shape(lhs_rank);
    if (lhs_rank > 0) {
      IREE_RETURN_IF_ERROR(
          iree_hal_buffer_view_shape(lhs, lhs_rank, lhs_shape.data(), nullptr));
    }

    iree_device_size_t rhs_size = iree_hal_buffer_view_byte_length(rhs);
    size_t rhs_rank = iree_hal_buffer_view_shape_rank(rhs);
    std::vector<iree_hal_dim_t> rhs_shape(rhs_rank);
    if (rhs_rank > 0) {
      IREE_RETURN_IF_ERROR(
          iree_hal_buffer_view_shape(rhs, rhs_rank, rhs_shape.data(), nullptr));
    }

    iree_hal_element_type_t lhs_element_type =
        iree_hal_buffer_view_element_type(lhs);
    iree_hal_element_type_t rhs_element_type =
        iree_hal_buffer_view_element_type(rhs);

    iree_hal_buffer_t* lhs_buf = iree_hal_buffer_view_buffer(lhs);
    iree_hal_buffer_mapping_t lhs_mapped_memory;
    IREE_RETURN_IF_ERROR(iree_hal_buffer_map_range(
        lhs_buf, IREE_HAL_MEMORY_ACCESS_READ,
        /*byte_offset=*/0, lhs_size, &lhs_mapped_memory));
    iree_hal_buffer_t* rhs_buf = iree_hal_buffer_view_buffer(rhs);
    iree_hal_buffer_mapping_t rhs_mapped_memory;
    IREE_RETURN_IF_ERROR(iree_hal_buffer_map_range(
        rhs_buf, IREE_HAL_MEMORY_ACCESS_READ,
        /*byte_offset=*/0, rhs_size, &rhs_mapped_memory));

    bool element_types_eq = lhs_element_type == rhs_element_type;
    bool shape_eq = lhs_shape == rhs_shape;
    // Only check contents if shape and element type match. Otherwise we can't.
    bool contents_could_be_almost_eq = true;
    if (element_types_eq && shape_eq) {
      IREE_ASSIGN_OR_RETURN(
          contents_could_be_almost_eq,
          AlmostEqByteSpan(lhs_mapped_memory.contents,
                           rhs_mapped_memory.contents, lhs_element_type));
    }
    iree_hal_buffer_unmap_range(&lhs_mapped_memory);
    iree_hal_buffer_unmap_range(&rhs_mapped_memory);

    if (!element_types_eq || !shape_eq || !contents_could_be_almost_eq) {
      std::ostringstream os;
      os << "Expected near equality of these values.";
      if (!element_types_eq) {
        os << " Element types do not match.";
      }
      if (!shape_eq) {
        os << " Shapes do not match.";
      }
      if (!contents_could_be_almost_eq) {
        os << " Contents does not match.";
      }
      // TODO(b/146898896): Propagate original variable names.
      os << "\n"
            "  lhs:\n"
            "    ";
      IREE_ASSIGN_OR_RETURN(auto lhs_str, BufferViewToString(lhs));
      os << lhs_str;

      os << "\n"
            "  rhs:\n"
            "    ";
      IREE_ASSIGN_OR_RETURN(auto rhs_str, BufferViewToString(rhs));
      os << rhs_str;

      // TODO(b/146898896): Use ADD_FAILURE_AT to propagate source location.
      ADD_FAILURE() << os.str();
    }

    return OkStatus();
  }

 private:
  // Allocator that the caller requested we use for any allocations we need to
  // perform during operation.
  iree_allocator_t allocator_ = iree_allocator_system();
};

// Function table mapping imported function names to their implementation.
// The signature of the target function is expected to match that in the
// check.imports.mlir file.
static const vm::NativeFunction<CheckModuleState> kCheckModuleFunctions[] = {
    vm::MakeNativeFunction("expect_true", &CheckModuleState::ExpectTrue),
    vm::MakeNativeFunction("expect_false", &CheckModuleState::ExpectFalse),
    vm::MakeNativeFunction("expect_all_true", &CheckModuleState::ExpectAllTrue),
    vm::MakeNativeFunction("expect_eq", &CheckModuleState::ExpectEq),
    vm::MakeNativeFunction("expect_almost_eq",
                           &CheckModuleState::ExpectAlmostEq),
};

// The module instance that will be allocated and reused across contexts.
// Any context-specific state must be stored in a state structure such as
// CheckModuleState below.
//
// Assumed thread-safe (by construction here, as it's immutable), though if more
// state is stored here it will need to be synchronized by the implementation.
class CheckModule final : public vm::NativeModule<CheckModuleState> {
 public:
  using vm::NativeModule<CheckModuleState>::NativeModule;

  // Creates per-context state when the module is added to a new context.
  // May be called from any thread.
  StatusOr<std::unique_ptr<CheckModuleState>> CreateState(
      iree_allocator_t allocator) override {
    auto state = std::make_unique<CheckModuleState>(allocator);
    return state;
  }
};

}  // namespace

// Note that while we are using C++ bindings internally we still expose the
// module as a C instance. This hides the details of our implementation.
extern "C" iree_status_t iree_check_module_create(
    iree_allocator_t allocator, iree_vm_module_t** out_module) {
  IREE_ASSERT_ARGUMENT(out_module);
  *out_module = NULL;
  auto module = std::make_unique<CheckModule>(
      "check", allocator,
      iree::span<const vm::NativeFunction<CheckModuleState>>(
          kCheckModuleFunctions));
  *out_module = module.release()->interface();
  return iree_ok_status();
}

}  // namespace iree
