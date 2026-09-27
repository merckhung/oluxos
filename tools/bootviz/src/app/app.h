// The bootviz application: GLFW window (or headless offscreen target),
// playback clock, keyboard control, Skia scene + Vulkan compositor.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "src/render/compositor.h"
#include "src/render/vk_context.h"
#include "src/scene/scene.h"

struct GLFWwindow;

namespace bootviz::app {

struct AppOptions {
  int width = 1600, height = 900;
  bool headless = false;
  std::string screenshot;       // headless: one PNG of --stage/--progress
  std::string screenshots_dir;  // headless: one PNG per stage
  int stage = 1;                // 1-based
  float progress = 0.75f;       // within the stage
  bool progress_set = false;    // else each stage's showcase moment
  float speed = 1.f;
  bool loop = true;
  std::string font_dir = "/usr/share/fonts";
  bool validation = false;
};

class App {
 public:
  App();
  ~App();
  int Run(const AppOptions& options);

 private:
  bool Init(std::string* error);
  void Shutdown();
  bool RenderFrame(float wall_time);
  int SaveScreenshot(const std::string& path);
  void OnKey(int key, int action);
  void OnResize();
  void Seek(float seconds);

  AppOptions opt_;
  GLFWwindow* window_ = nullptr;
  vk::VkContext ctx_;
  render::Compositor compositor_;
  scene::Fonts fonts_;
  std::unique_ptr<scene::Scene> scene_;
  std::vector<uint8_t> overlay_;
  uint64_t overlay_version_ = 0;
  float boot_time_ = 0.f;  // position in the boot sequence, seconds
  float hold_ = 0.f;       // time spent on the final frame before looping
  bool paused_ = false;
  bool resize_pending_ = false;
};

}  // namespace bootviz::app
