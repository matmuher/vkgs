#include <pygs/engine/engine.h>

#include <queue>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <algorithm>

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <pygs/scene/camera.h>
#include <pygs/scene/splats.h>

#include "pygs/engine/radixsort.h"
#include "pygs/engine/vulkan/context.h"
#include "pygs/engine/vulkan/swapchain.h"
#include "pygs/engine/vulkan/attachment.h"
#include "pygs/engine/vulkan/descriptor_layout.h"
#include "pygs/engine/vulkan/pipeline_layout.h"
#include "pygs/engine/vulkan/compute_pipeline.h"
#include "pygs/engine/vulkan/graphics_pipeline.h"
#include "pygs/engine/vulkan/render_pass.h"
#include "pygs/engine/vulkan/framebuffer.h"
#include "pygs/engine/vulkan/descriptor.h"
#include "pygs/engine/vulkan/buffer.h"
#include "pygs/engine/vulkan/cpu_buffer.h"
#include "pygs/engine/vulkan/uniform_buffer.h"
#include "pygs/engine/vulkan/shader/uniforms.h"
#include "pygs/engine/vulkan/shader/projection.h"
#include "pygs/engine/vulkan/shader/rank.h"
#include "pygs/engine/vulkan/shader/inverse_index.h"
#include "pygs/engine/vulkan/shader/splat.h"
#include "pygs/engine/vulkan/shader/color.h"

namespace pygs {
namespace {

void check_vk_result(VkResult err) {
  if (err == 0) return;
  std::cerr << "[imgui vulkan] Error: VkResult = " << err << std::endl;
  if (err < 0) abort();
}

glm::mat3 ToScaleMatrix3(const glm::vec3& s) {
  glm::mat3 m(1.f);
  m[0][0] = s[0];
  m[1][1] = s[1];
  m[2][2] = s[2];
  return m;
}

glm::mat4 ToScaleMatrix4(const glm::vec3& s) {
  glm::mat4 m(1.f);
  m[0][0] = s[0];
  m[1][1] = s[1];
  m[2][2] = s[2];
  return m;
}

glm::mat4 ToScaleMatrix4(float s) {
  glm::mat4 m(1.f);
  m[0][0] = s;
  m[1][1] = s;
  m[2][2] = s;
  return m;
}

glm::mat4 ToTranslationMatrix4(const glm::vec3& t) {
  glm::mat4 m(1.f);
  m[3][0] = t[0];
  m[3][1] = t[1];
  m[3][2] = t[2];
  return m;
}

}  // namespace

class Engine::Impl {
 public:
  Impl() {
    if (glfwInit() == GLFW_FALSE)
      throw std::runtime_error("Failed to initialize glfw.");

    context_ = vk::Context(0);

    // render pass
    render_pass_ = vk::RenderPass(context_);

    {
      vk::DescriptorLayoutCreateInfo descriptor_layout_info = {};
      descriptor_layout_info.bindings.resize(1);
      descriptor_layout_info.bindings[0] = {};
      descriptor_layout_info.bindings[0].binding = 0;
      descriptor_layout_info.bindings[0].descriptor_type =
          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptor_layout_info.bindings[0].stage_flags =
          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
      camera_descriptor_layout_ =
          vk::DescriptorLayout(context_, descriptor_layout_info);
    }

    {
      vk::DescriptorLayoutCreateInfo descriptor_layout_info = {};
      descriptor_layout_info.bindings.resize(5);
      descriptor_layout_info.bindings[0] = {};
      descriptor_layout_info.bindings[0].binding = 0;
      descriptor_layout_info.bindings[0].descriptor_type =
          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptor_layout_info.bindings[0].stage_flags =
          VK_SHADER_STAGE_COMPUTE_BIT;

      descriptor_layout_info.bindings[1] = {};
      descriptor_layout_info.bindings[1].binding = 1;
      descriptor_layout_info.bindings[1].descriptor_type =
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptor_layout_info.bindings[1].stage_flags =
          VK_SHADER_STAGE_COMPUTE_BIT;

      descriptor_layout_info.bindings[2] = {};
      descriptor_layout_info.bindings[2].binding = 2;
      descriptor_layout_info.bindings[2].descriptor_type =
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptor_layout_info.bindings[2].stage_flags =
          VK_SHADER_STAGE_COMPUTE_BIT;

      descriptor_layout_info.bindings[3] = {};
      descriptor_layout_info.bindings[3].binding = 3;
      descriptor_layout_info.bindings[3].descriptor_type =
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptor_layout_info.bindings[3].stage_flags =
          VK_SHADER_STAGE_COMPUTE_BIT;

      descriptor_layout_info.bindings[4] = {};
      descriptor_layout_info.bindings[4].binding = 4;
      descriptor_layout_info.bindings[4].descriptor_type =
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptor_layout_info.bindings[4].stage_flags =
          VK_SHADER_STAGE_COMPUTE_BIT;

      gaussian_descriptor_layout_ =
          vk::DescriptorLayout(context_, descriptor_layout_info);
    }

    {
      vk::DescriptorLayoutCreateInfo descriptor_layout_info = {};
      descriptor_layout_info.bindings.resize(6);
      descriptor_layout_info.bindings[0] = {};
      descriptor_layout_info.bindings[0].binding = 0;
      descriptor_layout_info.bindings[0].descriptor_type =
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptor_layout_info.bindings[0].stage_flags =
          VK_SHADER_STAGE_COMPUTE_BIT;

      descriptor_layout_info.bindings[1] = {};
      descriptor_layout_info.bindings[1].binding = 1;
      descriptor_layout_info.bindings[1].descriptor_type =
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptor_layout_info.bindings[1].stage_flags =
          VK_SHADER_STAGE_COMPUTE_BIT;

      descriptor_layout_info.bindings[2] = {};
      descriptor_layout_info.bindings[2].binding = 2;
      descriptor_layout_info.bindings[2].descriptor_type =
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptor_layout_info.bindings[2].stage_flags =
          VK_SHADER_STAGE_COMPUTE_BIT;

      descriptor_layout_info.bindings[3] = {};
      descriptor_layout_info.bindings[3].binding = 3;
      descriptor_layout_info.bindings[3].descriptor_type =
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptor_layout_info.bindings[3].stage_flags =
          VK_SHADER_STAGE_COMPUTE_BIT;

      descriptor_layout_info.bindings[4] = {};
      descriptor_layout_info.bindings[4].binding = 4;
      descriptor_layout_info.bindings[4].descriptor_type =
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptor_layout_info.bindings[4].stage_flags =
          VK_SHADER_STAGE_COMPUTE_BIT;

      descriptor_layout_info.bindings[5] = {};
      descriptor_layout_info.bindings[5].binding = 5;
      descriptor_layout_info.bindings[5].descriptor_type =
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptor_layout_info.bindings[5].stage_flags =
          VK_SHADER_STAGE_COMPUTE_BIT;

      instance_layout_ = vk::DescriptorLayout(context_, descriptor_layout_info);
    }

    // compute pipeline layout
    {
      vk::PipelineLayoutCreateInfo pipeline_layout_info = {};
      pipeline_layout_info.layouts = {camera_descriptor_layout_,
                                      gaussian_descriptor_layout_,
                                      instance_layout_};

      pipeline_layout_info.push_constants.resize(1);
      pipeline_layout_info.push_constants[0].stageFlags =
          VK_SHADER_STAGE_COMPUTE_BIT;
      pipeline_layout_info.push_constants[0].offset = 0;
      pipeline_layout_info.push_constants[0].size = sizeof(glm::mat4);

      compute_pipeline_layout_ =
          vk::PipelineLayout(context_, pipeline_layout_info);
    }

    // graphics pipeline layout
    {
      vk::PipelineLayoutCreateInfo pipeline_layout_info = {};
      pipeline_layout_info.layouts = {camera_descriptor_layout_};

      pipeline_layout_info.push_constants.resize(1);
      pipeline_layout_info.push_constants[0].stageFlags =
          VK_SHADER_STAGE_VERTEX_BIT;
      pipeline_layout_info.push_constants[0].offset = 0;
      pipeline_layout_info.push_constants[0].size = sizeof(glm::mat4);

      graphics_pipeline_layout_ =
          vk::PipelineLayout(context_, pipeline_layout_info);
    }

    // rank pipeline
    {
      vk::ComputePipelineCreateInfo pipeline_info = {};
      pipeline_info.layout = compute_pipeline_layout_;
      pipeline_info.compute_shader = vk::shader::rank_comp;
      rank_pipeline_ = vk::ComputePipeline(context_, pipeline_info);
    }

    // inverse index pipeline
    {
      vk::ComputePipelineCreateInfo pipeline_info = {};
      pipeline_info.layout = compute_pipeline_layout_;
      pipeline_info.compute_shader = vk::shader::inverse_index_comp;
      inverse_index_pipeline_ = vk::ComputePipeline(context_, pipeline_info);
    }

    // projection pipeline
    {
      vk::ComputePipelineCreateInfo pipeline_info = {};
      pipeline_info.layout = compute_pipeline_layout_;
      pipeline_info.compute_shader = vk::shader::projection_comp;
      projection_pipeline_ = vk::ComputePipeline(context_, pipeline_info);
    }

    // splat pipeline
    {
      std::vector<VkVertexInputBindingDescription> input_bindings(2);
      // xy
      input_bindings[0].binding = 0;
      input_bindings[0].stride = sizeof(float) * 2;
      input_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

      // ndc position, cov2d, rgba
      input_bindings[1].binding = 1;
      input_bindings[1].stride = sizeof(float) * 10;
      input_bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

      std::vector<VkVertexInputAttributeDescription> input_attributes(4);
      // vertex position
      input_attributes[0].location = 0;
      input_attributes[0].binding = 0;
      input_attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
      input_attributes[0].offset = 0;

      // cov2d
      input_attributes[1].location = 1;
      input_attributes[1].binding = 1;
      input_attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
      input_attributes[1].offset = 0;

      // projected position
      input_attributes[2].location = 2;
      input_attributes[2].binding = 1;
      input_attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
      input_attributes[2].offset = sizeof(float) * 3;

      // point rgba
      input_attributes[3].location = 3;
      input_attributes[3].binding = 1;
      input_attributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
      input_attributes[3].offset = sizeof(float) * 6;

      std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachments(
          1);
      color_blend_attachments[0] = {};
      color_blend_attachments[0].blendEnable = VK_TRUE;
      color_blend_attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      color_blend_attachments[0].dstColorBlendFactor =
          VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      color_blend_attachments[0].colorBlendOp = VK_BLEND_OP_ADD;
      color_blend_attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      color_blend_attachments[0].dstAlphaBlendFactor =
          VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      color_blend_attachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
      color_blend_attachments[0].colorWriteMask =
          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

      vk::GraphicsPipelineCreateInfo pipeline_info = {};
      pipeline_info.layout = graphics_pipeline_layout_;
      pipeline_info.render_pass = render_pass_;
      pipeline_info.vertex_shader = vk::shader::splat_vert;
      pipeline_info.fragment_shader = vk::shader::splat_frag;
      pipeline_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
      pipeline_info.input_bindings = std::move(input_bindings);
      pipeline_info.input_attributes = std::move(input_attributes);
      pipeline_info.depth_test = true;
      pipeline_info.depth_write = false;
      pipeline_info.color_blend_attachments =
          std::move(color_blend_attachments);
      splat_pipeline_ = vk::GraphicsPipeline(context_, pipeline_info);
    }

    // color pipeline
    {
      std::vector<VkVertexInputBindingDescription> input_bindings(2);
      // xyz
      input_bindings[0].binding = 0;
      input_bindings[0].stride = sizeof(float) * 3;
      input_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

      // rgba
      input_bindings[1].binding = 1;
      input_bindings[1].stride = sizeof(float) * 4;
      input_bindings[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

      std::vector<VkVertexInputAttributeDescription> input_attributes(2);
      // xyz
      input_attributes[0].location = 0;
      input_attributes[0].binding = 0;
      input_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
      input_attributes[0].offset = 0;

      // rgba
      input_attributes[1].location = 1;
      input_attributes[1].binding = 1;
      input_attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
      input_attributes[1].offset = 0;

      std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachments(
          1);
      color_blend_attachments[0] = {};
      color_blend_attachments[0].blendEnable = VK_TRUE;
      color_blend_attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      color_blend_attachments[0].dstColorBlendFactor =
          VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      color_blend_attachments[0].colorBlendOp = VK_BLEND_OP_ADD;
      color_blend_attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      color_blend_attachments[0].dstAlphaBlendFactor =
          VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      color_blend_attachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
      color_blend_attachments[0].colorWriteMask =
          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

      vk::GraphicsPipelineCreateInfo pipeline_info = {};
      pipeline_info.layout = graphics_pipeline_layout_;
      pipeline_info.render_pass = render_pass_;
      pipeline_info.vertex_shader = vk::shader::color_vert;
      pipeline_info.fragment_shader = vk::shader::color_frag;
      pipeline_info.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
      pipeline_info.input_bindings = std::move(input_bindings);
      pipeline_info.input_attributes = std::move(input_attributes);
      pipeline_info.depth_test = true;
      pipeline_info.depth_write = true;
      pipeline_info.color_blend_attachments =
          std::move(color_blend_attachments);
      color_line_pipeline_ = vk::GraphicsPipeline(context_, pipeline_info);
    }

    // uniforms and descriptors
    camera_buffer_ = vk::UniformBuffer<vk::shader::Camera>(context_, 2);
    num_element_cpu_buffer_ = vk::CpuBuffer(context_, 2 * sizeof(uint32_t));
    descriptors_.resize(2);
    for (int i = 0; i < 2; ++i) {
      descriptors_[i].camera =
          vk::Descriptor(context_, camera_descriptor_layout_);
      descriptors_[i].camera.Update(0, camera_buffer_, camera_buffer_.offset(i),
                                    camera_buffer_.element_size());

      descriptors_[i].gaussian =
          vk::Descriptor(context_, gaussian_descriptor_layout_);
      descriptors_[i].splat_instance =
          vk::Descriptor(context_, instance_layout_);
    }

    splat_buffer_.info = vk::UniformBuffer<vk::shader::SplatInfo>(context_, 1);
    splat_buffer_.num_elements = vk::Buffer(
        context_, sizeof(uint32_t),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    splat_buffer_.draw_indirect =
        vk::Buffer(context_, 5 * sizeof(uint32_t),
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

    // commands and synchronizations
    draw_command_buffers_.resize(3);
    VkCommandBufferAllocateInfo command_buffer_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_buffer_info.commandPool = context_.command_pool();
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = draw_command_buffers_.size();
    vkAllocateCommandBuffers(context_.device(), &command_buffer_info,
                             draw_command_buffers_.data());

    VkSemaphoreCreateInfo semaphore_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    render_finished_semaphores_.resize(2);
    render_finished_fences_.resize(2);
    for (int i = 0; i < 2; ++i) {
      vkCreateSemaphore(context_.device(), &semaphore_info, NULL,
                        &render_finished_semaphores_[i]);
      vkCreateFence(context_.device(), &fence_info, NULL,
                    &render_finished_fences_[i]);
    }

    image_acquired_semaphores_.resize(3);
    for (int i = 0; i < 3; ++i) {
      vkCreateSemaphore(context_.device(), &semaphore_info, NULL,
                        &image_acquired_semaphores_[i]);
    }

    {
      VkSemaphoreTypeCreateInfo semaphore_type_info = {
          VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
      semaphore_type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
      VkSemaphoreCreateInfo semaphore_info = {
          VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      semaphore_info.pNext = &semaphore_type_info;
      vkCreateSemaphore(context_.device(), &semaphore_info, NULL,
                        &transfer_semaphore_);
    }

    // create query pools
    timestamp_query_pools_.resize(2);
    for (int i = 0; i < 2; ++i) {
      VkQueryPoolCreateInfo query_pool_info = {
          VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
      query_pool_info.queryCount = timestamp_count_;
      vkCreateQueryPool(context_.device(), &query_pool_info, NULL,
                        &timestamp_query_pools_[i]);
    }

    // frame info
    frame_infos_.resize(2);

    PreparePrimitives();
  }

  ~Impl() {
    vkDeviceWaitIdle(context_.device());

    for (auto semaphore : image_acquired_semaphores_)
      vkDestroySemaphore(context_.device(), semaphore, NULL);
    for (auto semaphore : render_finished_semaphores_)
      vkDestroySemaphore(context_.device(), semaphore, NULL);
    for (auto fence : render_finished_fences_)
      vkDestroyFence(context_.device(), fence, NULL);
    vkDestroySemaphore(context_.device(), transfer_semaphore_, NULL);

    for (auto query_pool : timestamp_query_pools_)
      vkDestroyQueryPool(context_.device(), query_pool, NULL);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
  }

  void AddSplats(const Splats& splats) {
    uint32_t point_count = splats.size();

    const auto& position = splats.positions();
    const auto& sh = splats.sh();
    const auto& opacity = splats.opacity();
    const auto& rotation = splats.rots();
    const auto& scale = splats.scales();

    std::vector<float> gaussian_cov3d;
    gaussian_cov3d.reserve(6 * point_count);
    for (int i = 0; i < point_count; ++i) {
      glm::quat q(rotation[i * 4 + 0], rotation[i * 4 + 1], rotation[i * 4 + 2],
                  rotation[i * 4 + 3]);
      glm::mat3 r = glm::toMat3(q);
      glm::mat3 s = glm::mat4(1.f);
      s[0][0] = scale[i * 3 + 0];
      s[1][1] = scale[i * 3 + 1];
      s[2][2] = scale[i * 3 + 2];
      glm::mat3 m = r * s * s * glm::transpose(r);  // cov = RSSR^T

      gaussian_cov3d.push_back(m[0][0]);
      gaussian_cov3d.push_back(m[1][0]);
      gaussian_cov3d.push_back(m[2][0]);
      gaussian_cov3d.push_back(m[1][1]);
      gaussian_cov3d.push_back(m[2][1]);
      gaussian_cov3d.push_back(m[2][2]);
    }

    splat_buffer_.position = vk::Buffer(
        context_, position.size() * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    splat_buffer_.cov3d = vk::Buffer(
        context_, gaussian_cov3d.size() * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    splat_buffer_.opacity = vk::Buffer(
        context_, opacity.size() * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    splat_buffer_.sh = vk::Buffer(
        context_, sh.size() * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    splat_buffer_.key = vk::Buffer(context_, point_count * sizeof(uint32_t),
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    splat_buffer_.index = vk::Buffer(context_, point_count * sizeof(uint32_t),
                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    splat_buffer_.inverse_index = vk::Buffer(
        context_, point_count * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    splat_buffer_.instance = vk::Buffer(
        context_, point_count * 10 * sizeof(float),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    VkCommandBufferAllocateInfo command_buffer_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_buffer_info.commandPool = context_.command_pool();
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(context_.device(), &command_buffer_info, &cb);

    VkCommandBufferBeginInfo begin_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &begin_info);

    splat_buffer_.position.FromCpu(cb, position);
    splat_buffer_.cov3d.FromCpu(cb, gaussian_cov3d);
    splat_buffer_.opacity.FromCpu(cb, opacity);
    splat_buffer_.sh.FromCpu(cb, sh);

    vkEndCommandBuffer(cb);

    std::vector<VkCommandBufferSubmitInfo> command_buffer_submit_info(1);
    command_buffer_submit_info[0] = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command_buffer_submit_info[0].commandBuffer = cb;

    std::vector<VkSemaphoreSubmitInfo> wait_semaphore_info(1);
    wait_semaphore_info[0] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait_semaphore_info[0].semaphore = transfer_semaphore_;
    wait_semaphore_info[0].value = transfer_timeline_;
    wait_semaphore_info[0].stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

    std::vector<VkSemaphoreSubmitInfo> signal_semaphore_info(1);
    signal_semaphore_info[0] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal_semaphore_info[0].semaphore = transfer_semaphore_;
    signal_semaphore_info[0].value = transfer_timeline_ + 1;
    signal_semaphore_info[0].stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

    VkSubmitInfo2 submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit_info.waitSemaphoreInfoCount = wait_semaphore_info.size();
    submit_info.pWaitSemaphoreInfos = wait_semaphore_info.data();
    submit_info.commandBufferInfoCount = command_buffer_submit_info.size();
    submit_info.pCommandBufferInfos = command_buffer_submit_info.data();
    submit_info.signalSemaphoreInfoCount = signal_semaphore_info.size();
    submit_info.pSignalSemaphoreInfos = signal_semaphore_info.data();
    vkQueueSubmit2(context_.queue(), 1, &submit_info, NULL);

    transfer_timeline_++;

    // update descriptor
    for (int i = 0; i < 2; ++i) {
      descriptors_[i].gaussian.Update(0, splat_buffer_.info, 0,
                                      splat_buffer_.info.element_size());
      descriptors_[i].gaussian.Update(1, splat_buffer_.position, 0,
                                      splat_buffer_.position.size());
      descriptors_[i].gaussian.Update(2, splat_buffer_.cov3d, 0,
                                      splat_buffer_.cov3d.size());
      descriptors_[i].gaussian.Update(3, splat_buffer_.opacity, 0,
                                      splat_buffer_.opacity.size());
      descriptors_[i].gaussian.Update(4, splat_buffer_.sh, 0,
                                      splat_buffer_.sh.size());

      descriptors_[i].splat_instance.Update(0, splat_buffer_.draw_indirect, 0,
                                            splat_buffer_.draw_indirect.size());
      descriptors_[i].splat_instance.Update(1, splat_buffer_.instance, 0,
                                            splat_buffer_.instance.size());
      descriptors_[i].splat_instance.Update(2, splat_buffer_.num_elements, 0,
                                            splat_buffer_.num_elements.size());
      descriptors_[i].splat_instance.Update(3, splat_buffer_.key, 0,
                                            splat_buffer_.key.size());
      descriptors_[i].splat_instance.Update(4, splat_buffer_.index, 0,
                                            splat_buffer_.index.size());
      descriptors_[i].splat_instance.Update(5, splat_buffer_.inverse_index, 0,
                                            splat_buffer_.inverse_index.size());
    }

    // update uniform buffer
    splat_buffer_.point_count = point_count;
    splat_buffer_.info[0].point_count = point_count;

    // create sorter
    radix_sorter_ = Radixsort(context_, point_count);
  }

  void AddSplatsAsync(std::future<Splats>&& splats_future) {
    pending_splats_.push(std::move(splats_future));
  }

  void Run() {
    // create window
    width_ = 1600;
    height_ = 900;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(width_, height_, "pygs", NULL, NULL);

    // create swapchain
    VkSurfaceKHR surface;
    glfwCreateWindowSurface(context_.instance(), window_, NULL, &surface);
    swapchain_ = vk::Swapchain(context_, surface);

    color_attachment_ =
        vk::Attachment(context_, swapchain_.width(), swapchain_.height(),
                       VK_FORMAT_B8G8R8A8_UNORM, VK_SAMPLE_COUNT_4_BIT, false);
    depth_attachment_ = vk::Attachment(
        context_, swapchain_.width(), swapchain_.height(),
        VK_FORMAT_D24_UNORM_S8_UINT, VK_SAMPLE_COUNT_4_BIT, false);

    vk::FramebufferCreateInfo framebuffer_info;
    framebuffer_info.render_pass = render_pass_;
    framebuffer_info.width = swapchain_.width();
    framebuffer_info.height = swapchain_.height();
    framebuffer_info.image_specs = {color_attachment_.image_spec(),
                                    depth_attachment_.image_spec(),
                                    swapchain_.image_spec()};
    framebuffer_ = vk::Framebuffer(context_, framebuffer_info);

    // Setup Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window_, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = context_.instance();
    init_info.PhysicalDevice = context_.physical_device();
    init_info.Device = context_.device();
    init_info.QueueFamily = context_.queue_family_index();
    init_info.Queue = context_.queue();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = context_.descriptor_pool();
    init_info.RenderPass = render_pass_;
    init_info.Subpass = 0;
    init_info.MinImageCount = 4;
    init_info.ImageCount = swapchain_.image_count();
    init_info.MSAASamples = VK_SAMPLE_COUNT_4_BIT;
    init_info.Allocator = VK_NULL_HANDLE;
    init_info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&init_info);

    // main loop
    while (!glfwWindowShouldClose(window_)) {
      glfwPollEvents();

      // handle events
      if (!io.WantCaptureMouse) {
        bool left = io.MouseDown[ImGuiMouseButton_Left];
        bool right = io.MouseDown[ImGuiMouseButton_Right];
        float dx = io.MouseDelta.x;
        float dy = io.MouseDelta.y;

        if (left && !right) {
          camera_.Rotate(dx, dy);
        } else if (!left && right) {
          camera_.Translate(dx, dy);
        } else if (left && right) {
          camera_.Zoom(dy);
        }
      }

      int width, height;
      glfwGetFramebufferSize(window_, &width, &height);
      camera_.SetWindowSize(width, height);

      // handle pending splat load in order
      while (!pending_splats_.empty()) {
        auto& front = pending_splats_.front();

        using namespace std::chrono_literals;
        if (front.wait_for(0s) == std::future_status::ready) {
          auto splats = front.get();
          AddSplats(splats);
          pending_splats_.pop();
        } else {
          break;
        }
      }

      Draw();
    }
  }

 private:
  void PreparePrimitives() {
    std::vector<float> splat_vertex = {
        // xy, ccw in NDC space.
        -1.f, -1.f,  // 0
        -1.f, 1.f,   // 1
        1.f,  -1.f,  // 2
        1.f,  1.f,   // 3
    };
    std::vector<uint32_t> splat_index = {0, 1, 2, 3};

    std::vector<float> axis_position = {
        0.f, 0.f, 0.f, 1.f, 0.f, 0.f,  // x
        0.f, 0.f, 0.f, 0.f, 1.f, 0.f,  // y
        0.f, 0.f, 0.f, 0.f, 0.f, 1.f,  // z
    };
    std::vector<float> axis_color = {
        1.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f,  // x
        0.f, 1.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f,  // y
        0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f,  // z
    };
    std::vector<uint32_t> axis_index = {
        0, 1, 2, 3, 4, 5,
    };

    std::vector<float> grid_position;
    std::vector<float> grid_color;
    std::vector<uint32_t> grid_index;
    constexpr int grid_size = 10;
    for (int i = 0; i < grid_size * 2 + 1; ++i) {
      grid_index.push_back(4 * i + 0);
      grid_index.push_back(4 * i + 1);
      grid_index.push_back(4 * i + 2);
      grid_index.push_back(4 * i + 3);
    }
    for (int i = -grid_size; i <= grid_size; ++i) {
      float t = static_cast<float>(i) / grid_size;
      grid_position.push_back(-1.f);
      grid_position.push_back(0);
      grid_position.push_back(t);
      grid_color.push_back(0.5f);
      grid_color.push_back(0.5f);
      grid_color.push_back(0.5f);
      grid_color.push_back(1.f);

      grid_position.push_back(1.f);
      grid_position.push_back(0);
      grid_position.push_back(t);
      grid_color.push_back(0.5f);
      grid_color.push_back(0.5f);
      grid_color.push_back(0.5f);
      grid_color.push_back(1.f);

      grid_position.push_back(t);
      grid_position.push_back(0);
      grid_position.push_back(-1.f);
      grid_color.push_back(0.5f);
      grid_color.push_back(0.5f);
      grid_color.push_back(0.5f);
      grid_color.push_back(1.f);

      grid_position.push_back(t);
      grid_position.push_back(0);
      grid_position.push_back(1.f);
      grid_color.push_back(0.5f);
      grid_color.push_back(0.5f);
      grid_color.push_back(0.5f);
      grid_color.push_back(1.f);
    }

    splat_vertex_buffer_ = vk::Buffer(
        context_, splat_vertex.size() * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    splat_index_buffer_ = vk::Buffer(
        context_, splat_index.size() * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    axis_.position_buffer = vk::Buffer(
        context_, axis_position.size() * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    axis_.color_buffer = vk::Buffer(
        context_, axis_color.size() * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    axis_.index_buffer = vk::Buffer(
        context_, axis_index.size() * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    grid_.position_buffer = vk::Buffer(
        context_, grid_position.size() * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    grid_.color_buffer = vk::Buffer(
        context_, grid_color.size() * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    grid_.index_buffer = vk::Buffer(
        context_, grid_index.size() * sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    VkCommandBufferAllocateInfo command_buffer_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_buffer_info.commandPool = context_.command_pool();
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(context_.device(), &command_buffer_info, &cb);

    VkCommandBufferBeginInfo begin_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &begin_info);

    splat_vertex_buffer_.FromCpu(cb, splat_vertex);
    splat_index_buffer_.FromCpu(cb, splat_index);

    axis_.position_buffer.FromCpu(cb, axis_position);
    axis_.color_buffer.FromCpu(cb, axis_color);
    axis_.index_buffer.FromCpu(cb, axis_index);
    axis_.index_count = axis_index.size();

    grid_.position_buffer.FromCpu(cb, grid_position);
    grid_.color_buffer.FromCpu(cb, grid_color);
    grid_.index_buffer.FromCpu(cb, grid_index);
    grid_.index_count = grid_index.size();

    vkEndCommandBuffer(cb);

    std::vector<VkCommandBufferSubmitInfo> command_buffer_submit_info(1);
    command_buffer_submit_info[0] = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command_buffer_submit_info[0].commandBuffer = cb;

    std::vector<VkSemaphoreSubmitInfo> wait_semaphore_info(1);
    wait_semaphore_info[0] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait_semaphore_info[0].semaphore = transfer_semaphore_;
    wait_semaphore_info[0].value = transfer_timeline_;
    wait_semaphore_info[0].stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

    std::vector<VkSemaphoreSubmitInfo> signal_semaphore_info(1);
    signal_semaphore_info[0] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal_semaphore_info[0].semaphore = transfer_semaphore_;
    signal_semaphore_info[0].value = transfer_timeline_ + 1;
    signal_semaphore_info[0].stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

    VkSubmitInfo2 submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit_info.waitSemaphoreInfoCount = wait_semaphore_info.size();
    submit_info.pWaitSemaphoreInfos = wait_semaphore_info.data();
    submit_info.commandBufferInfoCount = command_buffer_submit_info.size();
    submit_info.pCommandBufferInfos = command_buffer_submit_info.data();
    submit_info.signalSemaphoreInfoCount = signal_semaphore_info.size();
    submit_info.pSignalSemaphoreInfos = signal_semaphore_info.data();
    vkQueueSubmit2(context_.queue(), 1, &submit_info, NULL);

    transfer_timeline_++;
  }

  void Draw() {
    // recreate swapchain if need resize
    if (swapchain_.ShouldRecreate()) {
      vkWaitForFences(context_.device(), render_finished_fences_.size(),
                      render_finished_fences_.data(), VK_TRUE, UINT64_MAX);
      swapchain_.Recreate();

      color_attachment_ = vk::Attachment(
          context_, swapchain_.width(), swapchain_.height(),
          VK_FORMAT_B8G8R8A8_UNORM, VK_SAMPLE_COUNT_4_BIT, false);
      depth_attachment_ = vk::Attachment(
          context_, swapchain_.width(), swapchain_.height(),
          VK_FORMAT_D24_UNORM_S8_UINT, VK_SAMPLE_COUNT_4_BIT, false);

      vk::FramebufferCreateInfo framebuffer_info;
      framebuffer_info.render_pass = render_pass_;
      framebuffer_info.width = swapchain_.width();
      framebuffer_info.height = swapchain_.height();
      framebuffer_info.image_specs = {color_attachment_.image_spec(),
                                      depth_attachment_.image_spec(),
                                      swapchain_.image_spec()};
      framebuffer_ = vk::Framebuffer(context_, framebuffer_info);
    }

    int32_t acquire_index = frame_counter_ % 3;
    int32_t frame_index = frame_counter_ % 2;
    VkSemaphore image_acquired_semaphore =
        image_acquired_semaphores_[acquire_index];
    VkSemaphore render_finished_semaphore =
        render_finished_semaphores_[frame_index];
    VkFence render_finished_fence = render_finished_fences_[frame_index];
    VkCommandBuffer cb = draw_command_buffers_[frame_index];
    VkQueryPool timestamp_query_pool = timestamp_query_pools_[frame_index];
    auto& frame_info = frame_infos_[frame_index];

    uint32_t image_index;
    if (swapchain_.AcquireNextImage(image_acquired_semaphore, &image_index)) {
      // get timestamps
      uint64_t rank_time = 0;
      uint64_t sort_time = 0;
      uint64_t inverse_time = 0;
      uint64_t projection_time = 0;
      uint64_t rendering_time = 0;
      uint64_t end_to_end_time = 0;

      if (frame_info.drew_splats) {
        std::vector<uint64_t> timestamps(timestamp_count_);
        vkGetQueryPoolResults(
            context_.device(), timestamp_query_pool, 0, timestamps.size(),
            timestamps.size() * sizeof(uint64_t), timestamps.data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

        rank_time = timestamps[2] - timestamps[1];
        sort_time = timestamps[4] - timestamps[3];
        inverse_time = timestamps[6] - timestamps[5];
        projection_time = timestamps[8] - timestamps[7];
        rendering_time = timestamps[10] - timestamps[9];
        end_to_end_time = timestamps[11] - timestamps[0];
      }

      glm::mat4 model(1.f);

      // draw ui
      {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const auto& io = ImGui::GetIO();
        if (ImGui::Begin("pygs")) {
          ImGui::Text("%d splats", splat_buffer_.point_count);

          const auto* num_elements_buffer =
              reinterpret_cast<const uint32_t*>(num_element_cpu_buffer_.data());
          uint32_t num_elements = num_elements_buffer[frame_index];
          float visible_points_ratio = splat_buffer_.point_count > 0
                                           ? static_cast<float>(num_elements) /
                                                 splat_buffer_.point_count *
                                                 100.f
                                           : 0.f;
          ImGui::Text("%d (%.2f%%) visible splats", num_elements,
                      visible_points_ratio);

          ImGui::Text("fps       : %7.3f", io.Framerate);
          ImGui::Text("            %7.3fms", 1e3 / io.Framerate);
          ImGui::Text("frame e2e : %7.3fms",
                      static_cast<double>(end_to_end_time) / 1e6);

          uint64_t total_time = rank_time + sort_time + inverse_time +
                                projection_time + rendering_time;
          ImGui::Text("total     : %7.3fms",
                      static_cast<double>(total_time) / 1e6);
          ImGui::Text("rank      : %7.3fms (%5.2f%%)",
                      static_cast<double>(rank_time) / 1e6,
                      static_cast<double>(rank_time) / total_time * 100.);
          ImGui::Text("sort      : %7.3fms (%5.2f%%)",
                      static_cast<double>(sort_time) / 1e6,
                      static_cast<double>(sort_time) / total_time * 100.);
          ImGui::Text("inverse   : %7.3fms (%5.2f%%)",
                      static_cast<double>(inverse_time) / 1e6,
                      static_cast<double>(inverse_time) / total_time * 100.);
          ImGui::Text("projection: %7.3fms (%5.2f%%)",
                      static_cast<double>(projection_time) / 1e6,
                      static_cast<double>(projection_time) / total_time * 100.);
          ImGui::Text("rendering : %7.3fms (%5.2f%%)",
                      static_cast<double>(rendering_time) / 1e6,
                      static_cast<double>(rendering_time) / total_time * 100.);

          static int vsync = 1;
          ImGui::Text("Vsync");
          ImGui::SameLine();
          ImGui::RadioButton("on", &vsync, 1);
          ImGui::SameLine();
          ImGui::RadioButton("off", &vsync, 0);

          if (vsync)
            swapchain_.SetVsync(true);
          else
            swapchain_.SetVsync(false);

          ImGui::Checkbox("Axis", &show_axis_);
          ImGui::SameLine();
          ImGui::Checkbox("Grid", &show_grid_);

          ImGui::Text("Translation");
          static glm::vec3 lt(0.f);
          ImGui::PushID("Translation");
          ImGui::DragFloat3("local", glm::value_ptr(lt), 0.01f);
          if (ImGui::IsItemDeactivated()) {
            translation_ += glm::toMat3(rotation_) * scale_ * lt;
            lt = glm::vec3(0.f);
          }

          static glm::vec3 gt(0.f);
          ImGui::DragFloat3("global", glm::value_ptr(gt), 0.01f);
          if (ImGui::IsItemDeactivated()) {
            translation_ += gt;
            gt = glm::vec3(0.f);
          }
          ImGui::PopID();

          ImGui::Text("Rotation");
          ImGui::PushID("Rotation");
          static glm::vec3 lr(0.f);
          ImGui::DragFloat3("local", glm::value_ptr(lr), 0.1f);
          glm::quat lq = glm::quat(glm::radians(lr));
          if (ImGui::IsItemDeactivated()) {
            rotation_ = rotation_ * lq;
            lr = glm::vec3(0.f);
            lq = glm::quat(1.f, 0.f, 0.f, 0.f);
          }

          static glm::vec3 gr(0.f);
          ImGui::DragFloat3("global", glm::value_ptr(gr), 0.1f);
          glm::quat gq = glm::quat(glm::radians(gr));
          if (ImGui::IsItemDeactivated()) {
            translation_ = gq * translation_;
            rotation_ = gq * rotation_;
            gr = glm::vec3(0.f);
            gq = glm::quat(1.f, 0.f, 0.f, 0.f);
          }
          ImGui::PopID();

          ImGui::Text("Scale");
          ImGui::PushID("Scale");
          static float scale = 1.f;
          ImGui::DragFloat("local", &scale, 0.01f, 0.1f, 10.f, "%.3f",
                           ImGuiSliderFlags_Logarithmic);
          if (ImGui::IsItemDeactivated()) {
            scale_ *= scale;
            scale = 1.f;
          }
          ImGui::PopID();

          model = ToScaleMatrix4(scale_ * scale) * glm::toMat4(gq) *
                  ToTranslationMatrix4(translation_ + gt) *
                  glm::toMat4(rotation_ * lq) * ToTranslationMatrix4(lt);
        }
        ImGui::End();
        ImGui::Render();
      }

      // record command buffer
      vkWaitForFences(context_.device(), 1, &render_finished_fence, VK_TRUE,
                      UINT64_MAX);
      vkResetFences(context_.device(), 1, &render_finished_fence);

      camera_buffer_[frame_index].projection = camera_.ProjectionMatrix();
      camera_buffer_[frame_index].view = camera_.ViewMatrix();
      camera_buffer_[frame_index].camera_position = camera_.Eye();
      camera_buffer_[frame_index].screen_size = {camera_.width(),
                                                 camera_.height()};

      VkCommandBufferBeginInfo command_begin_info = {
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      command_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(cb, &command_begin_info);

      vkCmdResetQueryPool(cb, timestamp_query_pool, 0, timestamp_count_);

      vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_NONE, timestamp_query_pool,
                           0);

      if (splat_buffer_.point_count != 0) {
        // rank
        {
          std::vector<VkBufferMemoryBarrier2> buffer_barriers(1);
          buffer_barriers[0] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[0].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
              VK_PIPELINE_STAGE_2_TRANSFER_BIT;
          buffer_barriers[0].srcAccessMask =
              VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
          buffer_barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
          buffer_barriers[0].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
          buffer_barriers[0].buffer = splat_buffer_.num_elements;
          buffer_barriers[0].offset = 0;
          buffer_barriers[0].size = splat_buffer_.num_elements.size();

          VkDependencyInfo barrier = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
          barrier.bufferMemoryBarrierCount = buffer_barriers.size();
          barrier.pBufferMemoryBarriers = buffer_barriers.data();
          vkCmdPipelineBarrier2(cb, &barrier);

          vkCmdFillBuffer(cb, splat_buffer_.num_elements, 0, sizeof(uint32_t),
                          0);

          buffer_barriers.resize(3);
          buffer_barriers[0] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
          buffer_barriers[0].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
          buffer_barriers[0].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
          buffer_barriers[0].buffer = splat_buffer_.num_elements;
          buffer_barriers[0].offset = 0;
          buffer_barriers[0].size = splat_buffer_.num_elements.size();

          buffer_barriers[1] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[1].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[1].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[1].dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[1].buffer = splat_buffer_.key;
          buffer_barriers[1].offset = 0;
          buffer_barriers[1].size = splat_buffer_.key.size();

          buffer_barriers[2] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[2].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[2].srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
          buffer_barriers[2].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[2].dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[2].buffer = splat_buffer_.index;
          buffer_barriers[2].offset = 0;
          buffer_barriers[2].size = splat_buffer_.index.size();

          barrier = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
          barrier.bufferMemoryBarrierCount = buffer_barriers.size();
          barrier.pBufferMemoryBarriers = buffer_barriers.data();
          vkCmdPipelineBarrier2(cb, &barrier);

          vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rank_pipeline_);

          std::vector<VkDescriptorSet> descriptors = {
              descriptors_[frame_index].camera,
              descriptors_[frame_index].gaussian,
              descriptors_[frame_index].splat_instance,
          };
          vkCmdBindDescriptorSets(
              cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_layout_, 0,
              descriptors.size(), descriptors.data(), 0, nullptr);

          vkCmdPushConstants(cb, compute_pipeline_layout_,
                             VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(model),
                             glm::value_ptr(model));

          vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               timestamp_query_pool, 1);

          constexpr int local_size = 256;
          vkCmdDispatch(
              cb, (splat_buffer_.point_count + local_size - 1) / local_size, 1,
              1);

          vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               timestamp_query_pool, 2);
        }

        // num_elements to CPU
        {
          std::vector<VkBufferMemoryBarrier2> buffer_barriers(1);
          buffer_barriers[0] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[0].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
          buffer_barriers[0].dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
          buffer_barriers[0].buffer = splat_buffer_.num_elements;
          buffer_barriers[0].offset = 0;
          buffer_barriers[0].size = splat_buffer_.num_elements.size();

          VkDependencyInfo barrier = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
          barrier.bufferMemoryBarrierCount = buffer_barriers.size();
          barrier.pBufferMemoryBarriers = buffer_barriers.data();
          vkCmdPipelineBarrier2(cb, &barrier);

          VkBufferCopy region = {};
          region.srcOffset = 0;
          region.dstOffset = sizeof(uint32_t) * frame_index;
          region.size = sizeof(uint32_t);
          vkCmdCopyBuffer(cb, splat_buffer_.num_elements,
                          num_element_cpu_buffer_, 1, &region);
        }

        // radix sort
        {
          std::vector<VkBufferMemoryBarrier2> buffer_barriers(3);
          buffer_barriers[0] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[0].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[0].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
          buffer_barriers[0].buffer = splat_buffer_.num_elements;
          buffer_barriers[0].offset = 0;
          buffer_barriers[0].size = splat_buffer_.num_elements.size();

          buffer_barriers[1] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[1].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[1].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[1].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
          buffer_barriers[1].buffer = splat_buffer_.key;
          buffer_barriers[1].offset = 0;
          buffer_barriers[1].size = splat_buffer_.key.size();

          buffer_barriers[2] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[2].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[2].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[2].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[2].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
          buffer_barriers[2].buffer = splat_buffer_.index;
          buffer_barriers[2].offset = 0;
          buffer_barriers[2].size = splat_buffer_.index.size();

          VkDependencyInfo barrier = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
          barrier.bufferMemoryBarrierCount = buffer_barriers.size();
          barrier.pBufferMemoryBarriers = buffer_barriers.data();
          vkCmdPipelineBarrier2(cb, &barrier);

          vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               timestamp_query_pool, 3);

          radix_sorter_.Sort(cb, frame_index, splat_buffer_.num_elements,
                             splat_buffer_.key, splat_buffer_.index);

          vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               timestamp_query_pool, 4);
        }

        // inverse map
        {
          std::vector<VkBufferMemoryBarrier2> buffer_barriers(1);
          buffer_barriers[0] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[0].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
          buffer_barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
          buffer_barriers[0].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
          buffer_barriers[0].buffer = splat_buffer_.inverse_index;
          buffer_barriers[0].offset = 0;
          buffer_barriers[0].size = splat_buffer_.inverse_index.size();

          VkDependencyInfo barrier = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
          barrier.bufferMemoryBarrierCount = buffer_barriers.size();
          barrier.pBufferMemoryBarriers = buffer_barriers.data();
          vkCmdPipelineBarrier2(cb, &barrier);

          vkCmdFillBuffer(cb, splat_buffer_.inverse_index, 0,
                          splat_buffer_.inverse_index.size(), -1);

          buffer_barriers.resize(3);
          buffer_barriers[0] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[0].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[0].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
          buffer_barriers[0].buffer = splat_buffer_.num_elements;
          buffer_barriers[0].offset = 0;
          buffer_barriers[0].size = splat_buffer_.num_elements.size();

          buffer_barriers[1] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[1].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[1].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[1].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
          buffer_barriers[1].buffer = splat_buffer_.index;
          buffer_barriers[1].offset = 0;
          buffer_barriers[1].size = splat_buffer_.index.size();

          buffer_barriers[2] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[2].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
          buffer_barriers[2].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
          buffer_barriers[2].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[2].dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[2].buffer = splat_buffer_.inverse_index;
          buffer_barriers[2].offset = 0;
          buffer_barriers[2].size = splat_buffer_.inverse_index.size();

          barrier = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
          barrier.bufferMemoryBarrierCount = buffer_barriers.size();
          barrier.pBufferMemoryBarriers = buffer_barriers.data();
          vkCmdPipelineBarrier2(cb, &barrier);

          std::vector<VkDescriptorSet> descriptors = {
              descriptors_[frame_index].camera,
              descriptors_[frame_index].gaussian,
              descriptors_[frame_index].splat_instance,
          };
          vkCmdBindDescriptorSets(
              cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_layout_, 0,
              descriptors.size(), descriptors.data(), 0, nullptr);

          vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                            inverse_index_pipeline_);

          vkCmdPushConstants(cb, compute_pipeline_layout_,
                             VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(model),
                             glm::value_ptr(model));

          vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               timestamp_query_pool, 5);

          constexpr int local_size = 256;
          vkCmdDispatch(
              cb, (splat_buffer_.point_count + local_size - 1) / local_size, 1,
              1);

          vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               timestamp_query_pool, 6);
        }

        // projection
        {
          std::vector<VkBufferMemoryBarrier2> buffer_barriers(4);
          buffer_barriers[0] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[0].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[0].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
          buffer_barriers[0].buffer = splat_buffer_.num_elements;
          buffer_barriers[0].offset = 0;
          buffer_barriers[0].size = splat_buffer_.num_elements.size();

          buffer_barriers[1] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[1].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[1].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[1].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
          buffer_barriers[1].buffer = splat_buffer_.inverse_index;
          buffer_barriers[1].offset = 0;
          buffer_barriers[1].size = splat_buffer_.inverse_index.size();

          buffer_barriers[2] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[2].srcStageMask =
              VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
          buffer_barriers[2].srcAccessMask =
              VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
          buffer_barriers[2].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[2].dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[2].buffer = splat_buffer_.instance;
          buffer_barriers[2].offset = 0;
          buffer_barriers[2].size = splat_buffer_.instance.size();

          buffer_barriers[3] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[3].srcStageMask =
              VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
          buffer_barriers[3].srcAccessMask =
              VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
          buffer_barriers[3].dstStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[3].dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[3].buffer = splat_buffer_.draw_indirect;
          buffer_barriers[3].offset = 0;
          buffer_barriers[3].size = splat_buffer_.draw_indirect.size();

          VkDependencyInfo barrier = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
          barrier.bufferMemoryBarrierCount = buffer_barriers.size();
          barrier.pBufferMemoryBarriers = buffer_barriers.data();
          vkCmdPipelineBarrier2(cb, &barrier);

          vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                            projection_pipeline_);

          vkCmdPushConstants(cb, compute_pipeline_layout_,
                             VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(model),
                             glm::value_ptr(model));

          vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               timestamp_query_pool, 7);

          constexpr int local_size = 256;
          vkCmdDispatch(
              cb, (splat_buffer_.point_count + local_size - 1) / local_size, 1,
              1);

          vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               timestamp_query_pool, 8);
        }

        // draw
        {
          std::vector<VkBufferMemoryBarrier2> buffer_barriers(2);
          buffer_barriers[0] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[0].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[0].dstStageMask =
              VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
          buffer_barriers[0].dstAccessMask =
              VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
          buffer_barriers[0].buffer = splat_buffer_.instance;
          buffer_barriers[0].offset = 0;
          buffer_barriers[0].size = splat_buffer_.instance.size();

          buffer_barriers[1] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
          buffer_barriers[1].srcStageMask =
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
          buffer_barriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
          buffer_barriers[1].dstStageMask =
              VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
          buffer_barriers[1].dstAccessMask =
              VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
          buffer_barriers[1].buffer = splat_buffer_.draw_indirect;
          buffer_barriers[1].offset = 0;
          buffer_barriers[1].size = splat_buffer_.draw_indirect.size();

          VkDependencyInfo barrier = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
          barrier.bufferMemoryBarrierCount = buffer_barriers.size();
          barrier.pBufferMemoryBarriers = buffer_barriers.data();
          vkCmdPipelineBarrier2(cb, &barrier);

          vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               timestamp_query_pool, 9);

          DrawNormalPass(cb, frame_index, swapchain_.width(),
                         swapchain_.height(),
                         swapchain_.image_view(image_index));

          vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                               timestamp_query_pool, 10);
        }
        frame_info.drew_splats = true;
      } else {
        DrawNormalPass(cb, frame_index, swapchain_.width(), swapchain_.height(),
                       swapchain_.image_view(image_index));
        frame_info.drew_splats = false;
      }

      vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           timestamp_query_pool, 11);

      vkEndCommandBuffer(cb);

      std::vector<VkSemaphoreSubmitInfo> wait_semaphores(2);
      wait_semaphores[0] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
      wait_semaphores[0].semaphore = image_acquired_semaphore;
      wait_semaphores[0].stageMask = 0;

      wait_semaphores[1] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
      wait_semaphores[1].semaphore = transfer_semaphore_;
      wait_semaphores[1].value = transfer_timeline_;
      wait_semaphores[1].stageMask = 0;

      std::vector<VkCommandBufferSubmitInfo> command_buffer_submit_info(1);
      command_buffer_submit_info[0] = {
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
      command_buffer_submit_info[0].commandBuffer = cb;

      std::vector<VkSemaphoreSubmitInfo> signal_semaphores(1);
      signal_semaphores[0] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
      signal_semaphores[0].semaphore = render_finished_semaphore;
      signal_semaphores[0].stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

      VkSubmitInfo2 submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
      submit_info.waitSemaphoreInfoCount = wait_semaphores.size();
      submit_info.pWaitSemaphoreInfos = wait_semaphores.data();
      submit_info.commandBufferInfoCount = command_buffer_submit_info.size();
      submit_info.pCommandBufferInfos = command_buffer_submit_info.data();
      submit_info.signalSemaphoreInfoCount = signal_semaphores.size();
      submit_info.pSignalSemaphoreInfos = signal_semaphores.data();
      vkQueueSubmit2(context_.queue(), 1, &submit_info, render_finished_fence);

      VkSwapchainKHR swapchain_handle = swapchain_;
      VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
      present_info.waitSemaphoreCount = 1;
      present_info.pWaitSemaphores = &render_finished_semaphore;
      present_info.swapchainCount = 1;
      present_info.pSwapchains = &swapchain_handle;
      present_info.pImageIndices = &image_index;
      vkQueuePresentKHR(context_.queue(), &present_info);

      frame_counter_++;
    }
  }

  void DrawNormalPass(VkCommandBuffer cb, uint32_t frame_index, uint32_t width,
                      uint32_t height, VkImageView target_image_view) {
    std::vector<VkClearValue> clear_values(2);
    clear_values[0].color.float32[0] = 0.0f;
    clear_values[0].color.float32[1] = 0.0f;
    clear_values[0].color.float32[2] = 0.0f;
    clear_values[0].color.float32[3] = 1.f;
    clear_values[1].depthStencil.depth = 1.f;

    std::vector<VkImageView> render_pass_attachments = {
        color_attachment_,
        depth_attachment_,
        target_image_view,
    };
    VkRenderPassAttachmentBeginInfo render_pass_attachments_info = {
        VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO};
    render_pass_attachments_info.attachmentCount =
        render_pass_attachments.size();
    render_pass_attachments_info.pAttachments = render_pass_attachments.data();

    VkRenderPassBeginInfo render_pass_begin_info = {
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    render_pass_begin_info.pNext = &render_pass_attachments_info;
    render_pass_begin_info.renderPass = render_pass_;
    render_pass_begin_info.framebuffer = framebuffer_;
    render_pass_begin_info.renderArea.offset = {0, 0};
    render_pass_begin_info.renderArea.extent = {width, height};
    render_pass_begin_info.clearValueCount = clear_values.size();
    render_pass_begin_info.pClearValues = clear_values.data();
    vkCmdBeginRenderPass(cb, &render_pass_begin_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.x = 0.f;
    viewport.y = 0.f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    vkCmdSetScissor(cb, 0, 1, &scissor);

    std::vector<VkDescriptorSet> descriptors = {
        descriptors_[frame_index].camera,
    };
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_pipeline_layout_, 0, descriptors.size(),
                            descriptors.data(), 0, nullptr);

    // draw axis and grid
    {
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        color_line_pipeline_);

      glm::mat4 model(1.f);
      model[0][0] = 10.f;
      model[1][1] = 10.f;
      model[2][2] = 10.f;
      vkCmdPushConstants(cb, graphics_pipeline_layout_,
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(model), &model);

      if (show_axis_) {
        std::vector<VkBuffer> vbs = {axis_.position_buffer, axis_.color_buffer};
        std::vector<VkDeviceSize> vb_offsets = {0, 0};
        vkCmdBindVertexBuffers(cb, 0, vbs.size(), vbs.data(),
                               vb_offsets.data());

        vkCmdBindIndexBuffer(cb, axis_.index_buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cb, axis_.index_count, 1, 0, 0, 0);
      }

      if (show_grid_) {
        std::vector<VkBuffer> vbs = {grid_.position_buffer, grid_.color_buffer};
        std::vector<VkDeviceSize> vb_offsets = {0, 0};
        vkCmdBindVertexBuffers(cb, 0, vbs.size(), vbs.data(),
                               vb_offsets.data());

        vkCmdBindIndexBuffer(cb, grid_.index_buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cb, grid_.index_count, 1, 0, 0, 0);
      }
    }

    // draw splat
    if (splat_buffer_.point_count != 0) {
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, splat_pipeline_);

      std::vector<VkBuffer> vbs = {splat_vertex_buffer_,
                                   splat_buffer_.instance};
      std::vector<VkDeviceSize> vb_offsets = {0, 0};
      vkCmdBindVertexBuffers(cb, 0, vbs.size(), vbs.data(), vb_offsets.data());

      vkCmdBindIndexBuffer(cb, splat_index_buffer_, 0, VK_INDEX_TYPE_UINT32);

      vkCmdDrawIndexedIndirect(cb, splat_buffer_.draw_indirect, 0, 1, 0);
    }

    // draw ui
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(draw_data, cb);

    vkCmdEndRenderPass(cb);
  }

  GLFWwindow* window_ = nullptr;
  int width_ = 0;
  int height_ = 0;

  Camera camera_;

  vk::Context context_;
  vk::Swapchain swapchain_;

  std::vector<VkCommandBuffer> draw_command_buffers_;
  std::vector<VkSemaphore> image_acquired_semaphores_;
  std::vector<VkSemaphore> render_finished_semaphores_;
  std::vector<VkFence> render_finished_fences_;

  vk::DescriptorLayout camera_descriptor_layout_;
  vk::DescriptorLayout gaussian_descriptor_layout_;
  vk::DescriptorLayout instance_layout_;
  vk::PipelineLayout compute_pipeline_layout_;
  vk::PipelineLayout graphics_pipeline_layout_;

  // preprocess
  vk::ComputePipeline rank_pipeline_;
  vk::ComputePipeline inverse_index_pipeline_;
  vk::ComputePipeline projection_pipeline_;
  Radixsort radix_sorter_;

  // normal pass
  vk::Framebuffer framebuffer_;
  vk::RenderPass render_pass_;
  vk::GraphicsPipeline color_line_pipeline_;
  vk::GraphicsPipeline splat_pipeline_;

  vk::Attachment color_attachment_;
  vk::Attachment depth_attachment_;

  vk::UniformBuffer<vk::shader::Camera> camera_buffer_;

  struct ColorObject {
    vk::Buffer position_buffer;
    vk::Buffer color_buffer;
    vk::Buffer index_buffer;
    int index_count;
  };
  ColorObject axis_;
  ColorObject grid_;

  struct FrameDescriptor {
    vk::Descriptor camera;
    vk::Descriptor gaussian;
    vk::Descriptor splat_instance;
  };
  std::vector<FrameDescriptor> descriptors_;

  struct FrameInfo {
    bool drew_splats = false;
  };
  std::vector<FrameInfo> frame_infos_;

  struct SplatBuffer {
    vk::UniformBuffer<vk::shader::SplatInfo> info;

    uint32_t point_count;

    vk::Buffer position;  // (N, 3)
    vk::Buffer cov3d;     // (N, 6)
    vk::Buffer opacity;   // (N)
    vk::Buffer sh;        // (N, 3, 16)

    vk::Buffer key;            // (N)
    vk::Buffer index;          // (N)
    vk::Buffer inverse_index;  // (N)

    vk::Buffer num_elements;   // (1)
    vk::Buffer draw_indirect;  // (5)
    vk::Buffer instance;       // (N, 10)
  };
  SplatBuffer splat_buffer_;

  glm::vec3 translation_{0.f, 0.f, 0.f};
  glm::quat rotation_{1.f, 0.f, 0.f, 0.f};
  float scale_{1.f};

  bool show_axis_ = true;
  bool show_grid_ = true;

  std::queue<std::future<Splats>> pending_splats_;

  vk::CpuBuffer num_element_cpu_buffer_;  // (2) for debug

  vk::Buffer splat_vertex_buffer_;  // gaussian2d quad
  vk::Buffer splat_index_buffer_;   // gaussian2d quad

  VkSemaphore transfer_semaphore_ = VK_NULL_HANDLE;
  uint64_t transfer_timeline_ = 0;

  // timestamp queries
  static constexpr uint32_t timestamp_count_ = 12;
  std::vector<VkQueryPool> timestamp_query_pools_;

  uint64_t frame_counter_ = 0;
};

Engine::Engine() : impl_(std::make_shared<Impl>()) {}

Engine::~Engine() = default;

void Engine::AddSplats(const Splats& splats) { impl_->AddSplats(splats); }

void Engine::AddSplatsAsync(std::future<Splats>&& splats_future) {
  impl_->AddSplatsAsync(std::move(splats_future));
}

void Engine::Run() { impl_->Run(); }

}  // namespace pygs
