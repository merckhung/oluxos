// Vulkan compositor for the 2D scene.
//
// Each frame: an animated circuit-board backdrop drawn by a fragment shader
// (it pulses with the current boot stage's accent colour), then the
// Skia-rendered scene as a full-screen premultiplied-alpha texture on top.
// The result is presented to a GLFW window or, headless, kept in an
// offscreen image that ReadPixels() copies out for screenshots.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "src/render/vk_context.h"

namespace bootviz::render {

// Push constants of the backdrop shader (see shaders/backdrop.frag).
struct BackdropParams {
  float time = 0.f;      // seconds
  float progress = 0.f;  // progress through the whole boot, 0..1
  float width = 0.f, height = 0.f;
  std::array<float, 4> accent{0.3f, 0.6f, 1.f, 1.f};  // rgb, intensity
};

struct FrameInput {
  BackdropParams backdrop;
  // Skia N32 premultiplied pixels (BGRA in memory), extent().width *
  // extent().height * 4 bytes, or null to draw the backdrop only.
  const uint8_t* overlay = nullptr;
  uint64_t overlay_version = 0;
};

class Compositor {
 public:
  enum class FrameResult { kOk, kResize, kError };

  ~Compositor();
  bool Init(vk::VkContext* ctx, std::string* error);
  void Shutdown();

  FrameResult DrawFrame(const FrameInput& in);
  bool Resize(uint32_t width, uint32_t height, std::string* error);

  // Headless mode: the last frame as tightly packed RGBA8.
  bool ReadPixels(std::vector<uint8_t>* rgba);

  VkExtent2D extent() const { return ctx_->extent(); }

 private:
  static constexpr int kFrames = 2;

  struct Frame {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
    vk::GpuBuffer overlay_staging;
    vk::GpuImage overlay;
    bool overlay_ready = false;
    uint64_t overlay_version = UINT64_MAX;
    VkDescriptorSet overlay_set = VK_NULL_HANDLE;
  };

  bool CreateRenderPass(std::string* error);
  bool CreatePipelines(std::string* error);
  bool CreateTargetResources(std::string* error);
  void DestroyTargetResources();
  void Record(Frame& f, uint32_t image_index, const FrameInput& in);

  vk::VkContext* ctx_ = nullptr;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout overlay_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout backdrop_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout overlay_layout_ = VK_NULL_HANDLE;
  VkPipeline backdrop_pipeline_ = VK_NULL_HANDLE;
  VkPipeline overlay_pipeline_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers_;
  std::vector<VkSemaphore> render_finished_;  // one per target image
  Frame frames_[kFrames];
  int frame_index_ = 0;
};

}  // namespace bootviz::render
