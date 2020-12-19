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

#ifndef IREE_HAL_VULKAN_DIRECT_COMMAND_BUFFER_H_
#define IREE_HAL_VULKAN_DIRECT_COMMAND_BUFFER_H_

// clang-format off: Must be included before all other headers:
#include "iree/hal/vulkan/vulkan_headers.h"
// clang-format on

#include "iree/hal/cc/command_buffer.h"
#include "iree/hal/vulkan/descriptor_pool_cache.h"
#include "iree/hal/vulkan/descriptor_set_arena.h"
#include "iree/hal/vulkan/dynamic_symbols.h"
#include "iree/hal/vulkan/handle_util.h"
#include "iree/hal/vulkan/native_descriptor_set.h"
#include "iree/hal/vulkan/native_event.h"
#include "iree/hal/vulkan/pipeline_executable.h"
#include "iree/hal/vulkan/pipeline_executable_layout.h"
#include "iree/hal/vulkan/vma_buffer.h"

namespace iree {
namespace hal {
namespace vulkan {

// Command buffer implementation that directly maps to VkCommandBuffer.
// This records the commands on the calling thread without additional threading
// indirection.
class DirectCommandBuffer final : public CommandBuffer {
 public:
  DirectCommandBuffer(iree_hal_command_buffer_mode_t mode,
                      iree_hal_command_category_t command_categories,
                      ref_ptr<DescriptorPoolCache> descriptor_pool_cache,
                      ref_ptr<VkCommandPoolHandle> command_pool,
                      VkCommandBuffer command_buffer);
  ~DirectCommandBuffer() override;

  VkCommandBuffer handle() const { return command_buffer_; }

  bool is_recording() const override { return is_recording_; }

  Status Begin() override;
  Status End() override;

  Status ExecutionBarrier(
      iree_hal_execution_stage_t source_stage_mask,
      iree_hal_execution_stage_t target_stage_mask,
      absl::Span<const iree_hal_memory_barrier_t> memory_barriers,
      absl::Span<const iree_hal_buffer_barrier_t> buffer_barriers) override;
  Status SignalEvent(Event* event,
                     iree_hal_execution_stage_t source_stage_mask) override;
  Status ResetEvent(Event* event,
                    iree_hal_execution_stage_t source_stage_mask) override;
  Status WaitEvents(
      absl::Span<Event*> events, iree_hal_execution_stage_t source_stage_mask,
      iree_hal_execution_stage_t target_stage_mask,
      absl::Span<const iree_hal_memory_barrier_t> memory_barriers,
      absl::Span<const iree_hal_buffer_barrier_t> buffer_barriers) override;

  Status FillBuffer(Buffer* target_buffer, iree_device_size_t target_offset,
                    iree_device_size_t length, const void* pattern,
                    size_t pattern_length) override;
  Status DiscardBuffer(Buffer* buffer) override;
  Status UpdateBuffer(const void* source_buffer,
                      iree_device_size_t source_offset, Buffer* target_buffer,
                      iree_device_size_t target_offset,
                      iree_device_size_t length) override;
  Status CopyBuffer(Buffer* source_buffer, iree_device_size_t source_offset,
                    Buffer* target_buffer, iree_device_size_t target_offset,
                    iree_device_size_t length) override;

  Status PushConstants(ExecutableLayout* executable_layout, size_t offset,
                       absl::Span<const uint32_t> values) override;

  Status PushDescriptorSet(
      ExecutableLayout* executable_layout, int32_t set,
      absl::Span<const iree_hal_descriptor_set_binding_t> bindings) override;
  Status BindDescriptorSet(
      ExecutableLayout* executable_layout, int32_t set,
      DescriptorSet* descriptor_set,
      absl::Span<const iree_device_size_t> dynamic_offsets) override;

  Status Dispatch(Executable* executable, int32_t entry_point,
                  std::array<uint32_t, 3> workgroups) override;
  Status DispatchIndirect(Executable* executable, int32_t entry_point,
                          Buffer* workgroups_buffer,
                          iree_device_size_t workgroups_offset) override;

 private:
  const ref_ptr<DynamicSymbols>& syms() const { return command_pool_->syms(); }

  StatusOr<NativeEvent*> CastEvent(Event* event) const;
  StatusOr<VmaBuffer*> CastBuffer(Buffer* buffer) const;
  StatusOr<VmaBuffer*> CastBuffer(iree_hal_buffer_t* buffer) const;
  StatusOr<NativeDescriptorSet*> CastDescriptorSet(
      DescriptorSet* descriptor_set) const;
  StatusOr<PipelineExecutableLayout*> CastExecutableLayout(
      ExecutableLayout* executable_layout) const;
  StatusOr<PipelineExecutable*> CastExecutable(Executable* executable) const;

  bool is_recording_ = false;
  ref_ptr<VkCommandPoolHandle> command_pool_;
  VkCommandBuffer command_buffer_;

  // TODO(b/140026716): may grow large - should try to reclaim or reuse.
  DescriptorSetArena descriptor_set_arena_;

  // The current descriptor set group in use by the command buffer, if any.
  // This must remain valid until all in-flight submissions of the command
  // buffer complete.
  DescriptorSetGroup descriptor_set_group_;
};

}  // namespace vulkan
}  // namespace hal
}  // namespace iree

#endif  // IREE_HAL_VULKAN_DIRECT_COMMAND_BUFFER_H_
