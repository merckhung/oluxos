// Pipeline and frame plumbing follow github.com/merckhung/twn_election's
// renderer (Apache-2.0), reduced to two full-screen passes.
#include "src/render/compositor.h"

#include <cstring>
#include <span>

#include "src/render/bootviz_shaders.h"

namespace bootviz::render {

using namespace ::bootviz::vk;  // Vulkan entry points
namespace {

VkShaderModule MakeModule(VkDevice dev, std::span<const uint32_t> code) {
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = code.size() * sizeof(uint32_t);
  ci.pCode = code.data();
  VkShaderModule m = VK_NULL_HANDLE;
  vkCreateShaderModule(dev, &ci, nullptr, &m);
  return m;
}

// A full-screen triangle pipeline (no vertex input) with optional
// premultiplied-alpha blending.
VkPipeline MakeFullscreenPipeline(VkDevice dev, VkRenderPass pass, VkPipelineLayout layout,
                                  std::span<const uint32_t> vert, std::span<const uint32_t> frag,
                                  bool blend) {
  VkShaderModule vs = MakeModule(dev, vert);
  VkShaderModule fs = MakeModule(dev, frag);
  VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
       vs, "main", nullptr},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
       VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr},
  };
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState att{};
  att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  if (blend) {  // premultiplied alpha (Skia N32)
    att.blendEnable = VK_TRUE;
    att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
  }
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &att;
  const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dy.dynamicStateCount = 2;
  dy.pDynamicStates = dyn;
  VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pci.stageCount = 2;
  pci.pStages = stages;
  pci.pVertexInputState = &vi;
  pci.pInputAssemblyState = &ia;
  pci.pViewportState = &vp;
  pci.pRasterizationState = &rs;
  pci.pMultisampleState = &ms;
  pci.pColorBlendState = &cb;
  pci.pDynamicState = &dy;
  pci.layout = layout;
  pci.renderPass = pass;
  VkPipeline pipeline = VK_NULL_HANDLE;
  vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);
  vkDestroyShaderModule(dev, vs, nullptr);
  vkDestroyShaderModule(dev, fs, nullptr);
  return pipeline;
}

void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                  VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src,
                  VkPipelineStageFlags dst) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = from;
  b.newLayout = to;
  b.srcAccessMask = src_access;
  b.dstAccessMask = dst_access;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
}

}  // namespace

Compositor::~Compositor() { Shutdown(); }

bool Compositor::Init(vk::VkContext* ctx, std::string* error) {
  ctx_ = ctx;
  VkDevice dev = ctx_->device();

  VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                          VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dl.bindingCount = 1;
  dl.pBindings = &binding;
  vkCreateDescriptorSetLayout(dev, &dl, nullptr, &overlay_set_layout_);

  VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BackdropParams)};
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pl.pushConstantRangeCount = 1;
  pl.pPushConstantRanges = &push;
  vkCreatePipelineLayout(dev, &pl, nullptr, &backdrop_layout_);
  pl.pushConstantRangeCount = 0;
  pl.setLayoutCount = 1;
  pl.pSetLayouts = &overlay_set_layout_;
  vkCreatePipelineLayout(dev, &pl, nullptr, &overlay_layout_);

  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFrames};
  VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dp.maxSets = kFrames;
  dp.poolSizeCount = 1;
  dp.pPoolSizes = &size;
  vkCreateDescriptorPool(dev, &dp, nullptr, &pool_);

  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = VK_FILTER_NEAREST;
  sci.minFilter = VK_FILTER_NEAREST;
  sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCreateSampler(dev, &sci, nullptr, &sampler_);

  for (Frame& f : frames_) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = ctx_->command_pool();
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vkAllocateCommandBuffers(dev, &ai, &f.cmd);
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(dev, &fi, nullptr, &f.fence);
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(dev, &si, nullptr, &f.image_available);
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &overlay_set_layout_;
    vkAllocateDescriptorSets(dev, &dai, &f.overlay_set);
  }
  return CreateRenderPass(error) && CreatePipelines(error) && CreateTargetResources(error);
}

bool Compositor::CreateRenderPass(std::string* error) {
  VkAttachmentDescription color{};
  color.format = ctx_->color_format();
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // the backdrop covers every pixel
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = ctx_->headless() ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                       : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &ref;
  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rp.attachmentCount = 1;
  rp.pAttachments = &color;
  rp.subpassCount = 1;
  rp.pSubpasses = &sub;
  rp.dependencyCount = 1;
  rp.pDependencies = &dep;
  if (vkCreateRenderPass(ctx_->device(), &rp, nullptr, &render_pass_) != VK_SUCCESS) {
    *error = "vkCreateRenderPass failed";
    return false;
  }
  return true;
}

bool Compositor::CreatePipelines(std::string* error) {
  VkDevice dev = ctx_->device();
  backdrop_pipeline_ = MakeFullscreenPipeline(dev, render_pass_, backdrop_layout_,
                                              shaders::fullscreen_vert(),
                                              shaders::backdrop_frag(), false);
  overlay_pipeline_ = MakeFullscreenPipeline(dev, render_pass_, overlay_layout_,
                                             shaders::fullscreen_vert(), shaders::overlay_frag(),
                                             true);
  if (!backdrop_pipeline_ || !overlay_pipeline_) {
    *error = "graphics pipeline creation failed";
    return false;
  }
  return true;
}

bool Compositor::CreateTargetResources(std::string* error) {
  const VkExtent2D e = ctx_->extent();
  if (e.width == 0 || e.height == 0) return true;
  for (VkImageView target : ctx_->target_views()) {
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = render_pass_;
    fci.attachmentCount = 1;
    fci.pAttachments = &target;
    fci.width = e.width;
    fci.height = e.height;
    fci.layers = 1;
    VkFramebuffer fb;
    vkCreateFramebuffer(ctx_->device(), &fci, nullptr, &fb);
    framebuffers_.push_back(fb);
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore sem;
    vkCreateSemaphore(ctx_->device(), &si, nullptr, &sem);
    render_finished_.push_back(sem);
  }
  const VkDeviceSize bytes = VkDeviceSize{e.width} * e.height * 4;
  for (Frame& f : frames_) {
    if (!ctx_->CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, &f.overlay_staging) ||
        !ctx_->CreateImage(e.width, e.height, VK_FORMAT_B8G8R8A8_UNORM,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &f.overlay)) {
      *error = "cannot create the overlay texture";
      return false;
    }
    f.overlay_ready = false;
    f.overlay_version = UINT64_MAX;
    VkDescriptorImageInfo ii{sampler_, f.overlay.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = f.overlay_set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(ctx_->device(), 1, &w, 0, nullptr);
  }
  return true;
}

void Compositor::DestroyTargetResources() {
  VkDevice dev = ctx_->device();
  for (VkFramebuffer fb : framebuffers_) vkDestroyFramebuffer(dev, fb, nullptr);
  for (VkSemaphore s : render_finished_) vkDestroySemaphore(dev, s, nullptr);
  framebuffers_.clear();
  render_finished_.clear();
  for (Frame& f : frames_) {
    ctx_->DestroyBuffer(&f.overlay_staging);
    ctx_->DestroyImage(&f.overlay);
  }
}

bool Compositor::Resize(uint32_t width, uint32_t height, std::string* error) {
  vkDeviceWaitIdle(ctx_->device());
  DestroyTargetResources();
  if (ctx_->headless()) ctx_->DestroyTarget();
  if (!ctx_->CreateTarget(width, height, error)) return false;
  return CreateTargetResources(error);
}

void Compositor::Shutdown() {
  if (!ctx_ || !ctx_->device()) return;
  VkDevice dev = ctx_->device();
  vkDeviceWaitIdle(dev);
  DestroyTargetResources();
  for (Frame& f : frames_) {
    if (f.fence) vkDestroyFence(dev, f.fence, nullptr);
    if (f.image_available) vkDestroySemaphore(dev, f.image_available, nullptr);
    f = Frame{};
  }
  for (VkPipeline p : {backdrop_pipeline_, overlay_pipeline_}) {
    if (p) vkDestroyPipeline(dev, p, nullptr);
  }
  if (sampler_) vkDestroySampler(dev, sampler_, nullptr);
  if (pool_) vkDestroyDescriptorPool(dev, pool_, nullptr);
  for (VkPipelineLayout l : {backdrop_layout_, overlay_layout_}) {
    if (l) vkDestroyPipelineLayout(dev, l, nullptr);
  }
  if (overlay_set_layout_) vkDestroyDescriptorSetLayout(dev, overlay_set_layout_, nullptr);
  if (render_pass_) vkDestroyRenderPass(dev, render_pass_, nullptr);
  ctx_ = nullptr;
}

void Compositor::Record(Frame& f, uint32_t image_index, const FrameInput& in) {
  VkCommandBuffer cmd = f.cmd;
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);

  const VkExtent2D e = ctx_->extent();
  if (in.overlay && f.overlay_version != in.overlay_version) {
    std::memcpy(f.overlay_staging.mapped, in.overlay, size_t{e.width} * e.height * 4);
    f.overlay_version = in.overlay_version;
    ImageBarrier(cmd, f.overlay.image,
                 f.overlay_ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                 : VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {e.width, e.height, 1};
    vkCmdCopyBufferToImage(cmd, f.overlay_staging.buffer, f.overlay.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    ImageBarrier(cmd, f.overlay.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    f.overlay_ready = true;
  }

  VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rbi.renderPass = render_pass_;
  rbi.framebuffer = framebuffers_[image_index];
  rbi.renderArea = {{0, 0}, e};
  vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vp{0, 0, static_cast<float>(e.width), static_cast<float>(e.height), 0, 1};
  VkRect2D sc{{0, 0}, e};
  vkCmdSetViewport(cmd, 0, 1, &vp);
  vkCmdSetScissor(cmd, 0, 1, &sc);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, backdrop_pipeline_);
  BackdropParams params = in.backdrop;
  params.width = static_cast<float>(e.width);
  params.height = static_cast<float>(e.height);
  vkCmdPushConstants(cmd, backdrop_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(params),
                     &params);
  vkCmdDraw(cmd, 3, 1, 0, 0);

  if (f.overlay_ready) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_layout_, 0, 1,
                            &f.overlay_set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
  }
  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);
}

Compositor::FrameResult Compositor::DrawFrame(const FrameInput& in) {
  VkDevice dev = ctx_->device();
  const VkExtent2D e = ctx_->extent();
  if (e.width == 0 || e.height == 0 || framebuffers_.empty()) return FrameResult::kResize;
  Frame& f = frames_[frame_index_];
  vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX);

  uint32_t image_index = 0;
  if (!ctx_->headless()) {
    VkResult r = vkAcquireNextImageKHR(dev, ctx_->swapchain(), UINT64_MAX, f.image_available,
                                       VK_NULL_HANDLE, &image_index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) return FrameResult::kResize;
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return FrameResult::kError;
  }
  vkResetFences(dev, 1, &f.fence);
  Record(f, image_index, in);

  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  if (!ctx_->headless()) {
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f.image_available;
    si.pWaitDstStageMask = &wait_stage;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &render_finished_[image_index];
  }
  si.commandBufferCount = 1;
  si.pCommandBuffers = &f.cmd;
  if (vkQueueSubmit(ctx_->queue(), 1, &si, f.fence) != VK_SUCCESS) return FrameResult::kError;

  FrameResult result = FrameResult::kOk;
  if (ctx_->headless()) {
    vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX);
  } else {
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &render_finished_[image_index];
    const VkSwapchainKHR swapchain = ctx_->swapchain();
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &image_index;
    const VkResult r = vkQueuePresentKHR(ctx_->queue(), &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) result = FrameResult::kResize;
    else if (r != VK_SUCCESS) result = FrameResult::kError;
  }
  frame_index_ = (frame_index_ + 1) % kFrames;
  return result;
}

bool Compositor::ReadPixels(std::vector<uint8_t>* rgba) {
  if (!ctx_->headless()) return false;
  const VkExtent2D e = ctx_->extent();
  const VkDeviceSize bytes = VkDeviceSize{e.width} * e.height * 4;
  vk::GpuBuffer readback;
  if (!ctx_->CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, &readback)) return false;
  VkCommandBuffer cmd = ctx_->BeginOneShot();
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {e.width, e.height, 1};
  vkCmdCopyImageToBuffer(cmd, ctx_->offscreen().image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         readback.buffer, 1, &copy);
  ctx_->EndOneShot(cmd);
  rgba->resize(bytes);
  std::memcpy(rgba->data(), readback.mapped, bytes);
  ctx_->DestroyBuffer(&readback);
  return true;
}

}  // namespace bootviz::render
