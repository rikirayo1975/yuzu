// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <vector>

#include "common/common_types.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_device.h"
#include "video_core/renderer_vulkan/vk_resource_manager.h"
#include "video_core/renderer_vulkan/wrapper.h"

namespace Vulkan {

// Prefer small grow rates to avoid saturating the descriptor pool with barely used pipelines.
constexpr std::size_t SETS_GROW_RATE = 0x20;

DescriptorAllocator::DescriptorAllocator(VKDescriptorPool& descriptor_pool,
                                         VkDescriptorSetLayout layout)
    : VKFencedPool{SETS_GROW_RATE}, descriptor_pool{descriptor_pool}, layout{layout} {}

DescriptorAllocator::~DescriptorAllocator() = default;

VkDescriptorSet DescriptorAllocator::Commit(VKFence& fence) {
    const std::size_t index = CommitResource(fence);
    return descriptors_allocations[index / SETS_GROW_RATE][index % SETS_GROW_RATE];
}

void DescriptorAllocator::Allocate(std::size_t begin, std::size_t end) {
    descriptors_allocations.push_back(descriptor_pool.AllocateDescriptors(layout, end - begin));
}

VKDescriptorPool::VKDescriptorPool(const VKDevice& device)
    : device{device}, active_pool{AllocateNewPool()} {}

VKDescriptorPool::~VKDescriptorPool() = default;

vk::DescriptorPool* VKDescriptorPool::AllocateNewPool() {
    static constexpr u32 num_sets = 0x20000;
    static constexpr VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, num_sets * 90},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, num_sets * 60},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, num_sets * 64},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, num_sets * 64},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, num_sets * 40}};

    VkDescriptorPoolCreateInfo ci;
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.pNext = nullptr;
    ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.maxSets = num_sets;
    ci.poolSizeCount = static_cast<u32>(std::size(pool_sizes));
    ci.pPoolSizes = std::data(pool_sizes);
    return &pools.emplace_back(device.GetLogical().CreateDescriptorPool(ci));
}

vk::DescriptorSets VKDescriptorPool::AllocateDescriptors(VkDescriptorSetLayout layout,
                                                         std::size_t count) {
    const std::vector layout_copies(count, layout);
    VkDescriptorSetAllocateInfo ai;
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.pNext = nullptr;
    ai.descriptorPool = **active_pool;
    ai.descriptorSetCount = static_cast<u32>(count);
    ai.pSetLayouts = layout_copies.data();

    vk::DescriptorSets sets = active_pool->Allocate(ai);
    if (!sets.IsOutOfPoolMemory()) {
        return sets;
    }

    // Our current pool is out of memory. Allocate a new one and retry
    active_pool = AllocateNewPool();
    ai.descriptorPool = **active_pool;
    sets = active_pool->Allocate(ai);
    if (!sets.IsOutOfPoolMemory()) {
        return sets;
    }

    // After allocating a new pool, we are out of memory again. We can't handle this from here.
    throw vk::Exception(VK_ERROR_OUT_OF_POOL_MEMORY);
}

} // namespace Vulkan
