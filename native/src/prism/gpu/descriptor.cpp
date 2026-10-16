#include "prism/gpu/descriptor.hpp"

#include "prism/gpu/as.hpp"
#include "prism/gpu/buffer.hpp"
#include "prism/gpu/device.hpp"
#include "prism/gpu/image.hpp"
#include "prism/gpu/retire.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace gpu {
namespace {

bool isDynamic(VkDescriptorType type) {
    return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

} // namespace

// ---- DescriptorTable

DescriptorTable::DescriptorTable(std::shared_ptr<Device> device,
                                 VkDescriptorPool descriptorPool,
                                 std::vector<VkDescriptorSetLayout> tableLayout,
                                 std::vector<VkDescriptorSet> table,
                                 std::vector<std::vector<VkDescriptorType>> tableTypes,
                                 std::vector<VkPushConstantRange> pushConstantRanges)
    : device_(std::move(device)),
      descriptorPool_(descriptorPool),
      tableLayout_(std::move(tableLayout)),
      table_(std::move(table)),
      tableTypes_(std::move(tableTypes)),
      pushConstantRanges_(std::move(pushConstantRanges)) {
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = static_cast<uint32_t>(tableLayout_.size());
    info.pSetLayouts = tableLayout_.data();
    info.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges_.size());
    info.pPushConstantRanges = pushConstantRanges_.data();
    if (vkCreatePipelineLayout(device_->vkDevice(), &info, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("pipeline layout could not be made");
    }
}

DescriptorTable::~DescriptorTable() {
    retire::later([device = device_, layouts = tableLayout_, pipelineLayout = pipelineLayout_,
                   pool = descriptorPool_] {
        for (VkDescriptorSetLayout layout : layouts) vkDestroyDescriptorSetLayout(device->vkDevice(), layout, nullptr);
        vkDestroyPipelineLayout(device->vkDevice(), pipelineLayout, nullptr);
        vkDestroyDescriptorPool(device->vkDevice(), pool, nullptr);
    });
}

uint32_t DescriptorTable::setCount() {
    return static_cast<uint32_t>(table_.size());
}

uint32_t DescriptorTable::dynamicDescriptorCount() const {
    uint32_t count = 0;
    for (const auto &types : tableTypes_) {
        for (VkDescriptorType type : types) count += isDynamic(type) ? 1 : 0;
    }
    return count;
}

std::vector<VkDescriptorSet> &DescriptorTable::descriptorSet() {
    return table_;
}

std::vector<VkDescriptorSetLayout> &DescriptorTable::descriptorSetLayout() {
    return tableLayout_;
}

VkPipelineLayout &DescriptorTable::vkPipelineLayout() {
    return pipelineLayout_;
}

VkWriteDescriptorSet DescriptorTable::write(uint32_t set, uint32_t binding, uint32_t index, uint32_t count) const {
    if (set >= table_.size() || binding >= tableTypes_[set].size()) {
        throw std::out_of_range("descriptor binding " + std::to_string(set) + "." + std::to_string(binding) +
                                " is not in the table");
    }
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = table_[set];
    write.dstBinding = binding;
    write.dstArrayElement = index;
    write.descriptorCount = count;
    write.descriptorType = tableTypes_[set][binding];
    return write;
}

std::shared_ptr<DescriptorTable> DescriptorTable::bindImage(
    std::shared_ptr<Image> image, VkImageLayout layout, uint32_t set, uint32_t binding, uint32_t viewIndex) {
    VkDescriptorImageInfo info{VK_NULL_HANDLE, image->vkImageView(viewIndex), layout};
    VkWriteDescriptorSet w = write(set, binding, 0, 1);
    w.pImageInfo = &info;
    vkUpdateDescriptorSets(device_->vkDevice(), 1, &w, 0, nullptr);
    return shared_from_this();
}

std::shared_ptr<DescriptorTable> DescriptorTable::bindSamplerImage(std::shared_ptr<Sampler> sampler,
                                                                   std::shared_ptr<Image> image,
                                                                   VkImageLayout layout,
                                                                   uint32_t set,
                                                                   uint32_t binding,
                                                                   uint32_t index,
                                                                   uint32_t viewIndex) {
    VkDescriptorImageInfo info{sampler->vkSamper(), image->vkImageView(viewIndex), layout};
    VkWriteDescriptorSet w = write(set, binding, index, 1);
    w.pImageInfo = &info;
    vkUpdateDescriptorSets(device_->vkDevice(), 1, &w, 0, nullptr);
    return shared_from_this();
}

std::shared_ptr<DescriptorTable>
DescriptorTable::bindImageForShader(std::shared_ptr<Image> image, uint32_t set, uint32_t binding, uint32_t viewIndex) {
    return bindImage(std::move(image), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, set, binding, viewIndex);
}

std::shared_ptr<DescriptorTable> DescriptorTable::bindSamplerImageForShader(std::shared_ptr<Sampler> sampler,
                                                                            std::shared_ptr<Image> image,
                                                                            uint32_t set,
                                                                            uint32_t binding,
                                                                            uint32_t viewIndex) {
    return bindSamplerImage(std::move(sampler), std::move(image), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, set,
                            binding, 0, viewIndex);
}

std::shared_ptr<DescriptorTable>
DescriptorTable::bindBuffer(std::shared_ptr<Buffer> buffer, uint32_t set, uint32_t binding) {
    const VkDeviceSize size = buffer->size();
    return bindBufferRange(std::move(buffer), set, binding, 0, 0, size);
}

std::shared_ptr<DescriptorTable>
DescriptorTable::bindBuffer(std::shared_ptr<Buffer> buffer, uint32_t set, uint32_t binding, uint32_t index) {
    const VkDeviceSize size = buffer->size();
    return bindBufferRange(std::move(buffer), set, binding, index, 0, size);
}

std::shared_ptr<DescriptorTable> DescriptorTable::bindBufferRange(
    std::shared_ptr<Buffer> buffer, uint32_t set, uint32_t binding, VkDeviceSize offset, VkDeviceSize range) {
    return bindBufferRange(std::move(buffer), set, binding, 0, offset, range);
}

std::shared_ptr<DescriptorTable> DescriptorTable::bindBufferRange(std::shared_ptr<Buffer> buffer,
                                                                  uint32_t set,
                                                                  uint32_t binding,
                                                                  uint32_t index,
                                                                  VkDeviceSize offset,
                                                                  VkDeviceSize range) {
    VkDescriptorBufferInfo info{buffer->vkBuffer(), offset, range};
    VkWriteDescriptorSet w = write(set, binding, index, 1);
    w.pBufferInfo = &info;
    vkUpdateDescriptorSets(device_->vkDevice(), 1, &w, 0, nullptr);
    return shared_from_this();
}

std::shared_ptr<DescriptorTable>
DescriptorTable::bindBuffers(std::vector<std::shared_ptr<Buffer>> buffers, uint32_t set, uint32_t binding) {
    std::vector<VkDescriptorBufferInfo> infos;
    infos.reserve(buffers.size());
    for (const auto &buffer : buffers) infos.push_back({buffer->vkBuffer(), 0, buffer->size()});
    VkWriteDescriptorSet w = write(set, binding, 0, static_cast<uint32_t>(infos.size()));
    w.pBufferInfo = infos.data();
    vkUpdateDescriptorSets(device_->vkDevice(), 1, &w, 0, nullptr);
    return shared_from_this();
}

std::shared_ptr<DescriptorTable> DescriptorTable::bindAS(std::shared_ptr<TLAS> tlas, uint32_t set, uint32_t binding) {
    VkWriteDescriptorSetAccelerationStructureKHR info{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    info.accelerationStructureCount = 1;
    info.pAccelerationStructures = &tlas->handle();
    VkWriteDescriptorSet w = write(set, binding, 0, 1);
    w.pNext = &info;
    vkUpdateDescriptorSets(device_->vkDevice(), 1, &w, 0, nullptr);
    return shared_from_this();
}

// ---- DescriptorTableBuilder

DescriptorTableBuilder::DescriptorLayoutSetBindingBuilder::DescriptorLayoutSetBindingBuilder(
    DescriptorLayoutSetBuilder &parent)
    : parent(parent) {}

DescriptorTableBuilder::DescriptorLayoutSetBindingBuilder &
DescriptorTableBuilder::DescriptorLayoutSetBindingBuilder::defineDescriptorLayoutSetBinding(
    VkDescriptorSetLayoutBinding binding) {
    bindings.push_back(binding);
    types.push_back(binding.descriptorType);
    return *this;
}

DescriptorTableBuilder::DescriptorLayoutSetBuilder &
DescriptorTableBuilder::DescriptorLayoutSetBindingBuilder::endDescriptorLayoutSetBinding() {
    return parent;
}

DescriptorTableBuilder::DescriptorLayoutSetBuilder::DescriptorLayoutSetBuilder(DescriptorTableBuilder &parent)
    : parent(parent) {}

DescriptorTableBuilder::DescriptorLayoutSetBindingBuilder &
DescriptorTableBuilder::DescriptorLayoutSetBuilder::beginDescriptorLayoutSetBinding() {
    return setBindingBuilders.emplace_back(*this);
}

DescriptorTableBuilder &DescriptorTableBuilder::DescriptorLayoutSetBuilder::endDescriptorLayoutSet() {
    return parent;
}

DescriptorTableBuilder::DescriptorTableBuilder() : setBuilders(*this) {}

DescriptorTableBuilder::DescriptorLayoutSetBuilder &DescriptorTableBuilder::beginDescriptorLayoutSet() {
    return setBuilders;
}

DescriptorTableBuilder &DescriptorTableBuilder::definePushConstant(VkPushConstantRange pushConstantRange) {
    pushConstantRanges.push_back(pushConstantRange);
    return *this;
}

std::shared_ptr<DescriptorTable> DescriptorTableBuilder::build(std::shared_ptr<Device> device) {
    std::vector<VkDescriptorSetLayout> layouts;
    std::map<VkDescriptorType, uint32_t> typeCounts;
    bool updateAfterBind = false;

    for (const auto &set : setBuilders.setBindingBuilders) {
        // Sets without dynamic buffers may be rewritten while a frame that uses them is in flight, and may leave
        // bindings a pipeline never reads unwritten.
        bool dynamic = false;
        for (const auto &binding : set.bindings) dynamic |= isDynamic(binding.descriptorType);
        std::vector<VkDescriptorBindingFlags> flags(
            set.bindings.size(), dynamic ? 0
                                         : VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                               VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
                                               VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
        updateAfterBind |= !dynamic;

        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        flagsInfo.bindingCount = static_cast<uint32_t>(flags.size());
        flagsInfo.pBindingFlags = flags.data();

        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.pNext = &flagsInfo;
        info.flags = dynamic ? 0 : VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        info.bindingCount = static_cast<uint32_t>(set.bindings.size());
        info.pBindings = set.bindings.data();
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        if (vkCreateDescriptorSetLayout(device->vkDevice(), &info, nullptr, &layout) != VK_SUCCESS) {
            throw std::runtime_error("descriptor set layout could not be made");
        }
        layouts.push_back(layout);
        for (const auto &binding : set.bindings) typeCounts[binding.descriptorType] += binding.descriptorCount;
    }

    std::vector<VkDescriptorPoolSize> poolSizes;
    for (const auto &[type, count] : typeCounts) poolSizes.push_back({type, count});
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = updateAfterBind ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT : 0;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(layouts.size());
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device->vkDevice(), &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("descriptor pool could not be made");
    }

    std::vector<VkDescriptorSet> sets(layouts.size());
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device->vkDevice(), &allocInfo, sets.data()) != VK_SUCCESS) {
        throw std::runtime_error("descriptor sets could not be allocated");
    }

    // Types indexed by binding number (bindings may skip numbers).
    std::vector<std::vector<VkDescriptorType>> types;
    for (const auto &set : setBuilders.setBindingBuilders) {
        uint32_t highest = 0;
        for (const auto &binding : set.bindings) highest = std::max(highest, binding.binding);
        auto &setTypes = types.emplace_back(highest + 1, VK_DESCRIPTOR_TYPE_MAX_ENUM);
        for (const auto &binding : set.bindings) setTypes[binding.binding] = binding.descriptorType;
    }

    return DescriptorTable::create(device, pool, layouts, sets, types, pushConstantRanges);
}

} // namespace gpu
