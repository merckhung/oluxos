#include "src/app/app.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"
#include "stb_image_write.h"

namespace bootviz::app {
namespace {

App* FromWindow(GLFWwindow* w) { return static_cast<App*>(glfwGetWindowUserPointer(w)); }

constexpr float kHoldAtEnd = 5.f;  // seconds on the final frame before looping

// "07_sbin_init.png" for stage 7 ("/sbin/init").
std::string ShotName(int stage) {
  char prefix[16];
  std::snprintf(prefix, sizeof prefix, "%02d_", stage + 1);
  std::string name;
  for (char ch : scene::BootStages()[stage].name) {
    const bool keep = std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-';
    if (keep) name += ch;
    else if (!name.empty() && name.back() != '_') name += '_';
  }
  return prefix + name + ".png";
}

}  // namespace

App::App() = default;

App::~App() { Shutdown(); }

int App::Run(const AppOptions& options) {
  opt_ = options;
  std::string error;
  if (!Init(&error)) {
    std::fprintf(stderr, "bootviz: %s\n", error.c_str());
    return 1;
  }
  const int nstages = static_cast<int>(scene::BootStages().size());
  if (opt_.headless) {
    if (!opt_.screenshots_dir.empty()) {
      for (int i = 0; i < nstages; ++i) {
        const float progress =
            opt_.progress_set ? opt_.progress : scene::BootStages()[i].showcase;
        Seek(scene::SecondsAt({i, progress}));
        if (!RenderFrame(boot_time_)) return 1;
        if (int rc = SaveScreenshot(opt_.screenshots_dir + "/" + ShotName(i))) return rc;
      }
      return 0;
    }
    const int stage = std::clamp(opt_.stage, 1, nstages) - 1;
    const float progress =
        opt_.progress_set ? opt_.progress : scene::BootStages()[stage].showcase;
    Seek(scene::SecondsAt({stage, std::clamp(progress, 0.f, 1.f)}));
    if (!RenderFrame(boot_time_)) return 1;
    return opt_.screenshot.empty() ? 0 : SaveScreenshot(opt_.screenshot);
  }

  Seek(scene::SecondsAt({std::clamp(opt_.stage, 1, nstages) - 1, 0.f}));
  double last = glfwGetTime();
  while (!glfwWindowShouldClose(window_)) {
    glfwPollEvents();
    const double now = glfwGetTime();
    const float dt = static_cast<float>(std::min(0.1, now - last));
    last = now;
    if (!paused_) {
      if (boot_time_ < scene::TotalDuration()) {
        boot_time_ = std::min(scene::TotalDuration(), boot_time_ + dt * opt_.speed);
      } else if (opt_.loop && (hold_ += dt) > kHoldAtEnd) {
        Seek(0.f);
      }
    }
    if (resize_pending_) OnResize();
    if (!RenderFrame(static_cast<float>(now))) resize_pending_ = true;
  }
  return 0;
}

bool App::Init(std::string* error) {
  if (!fonts_.Load(opt_.font_dir, error)) return false;
  scene_ = std::make_unique<scene::Scene>(&fonts_);

  vk::VkContext::Options vo;
  vo.headless = opt_.headless;
  vo.width = opt_.width;
  vo.height = opt_.height;
  vo.validation = opt_.validation;
  vo.msaa = 1;  // 2D: Skia antialiases itself
  if (!opt_.headless) {
    if (!glfwInit()) {
      *error = "glfwInit failed (no display?); try --headless --screenshot=out.png";
      return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(opt_.width, opt_.height, "OluxOS boot on Raspberry Pi 4", nullptr,
                               nullptr);
    if (!window_) {
      *error = "cannot create a window";
      return false;
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, [](GLFWwindow* w, int k, int, int a, int) {
      FromWindow(w)->OnKey(k, a);
    });
    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int, int) {
      FromWindow(w)->resize_pending_ = true;
    });
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(window_, &fw, &fh);
    vo.width = fw;
    vo.height = fh;
  }
  if (!ctx_.Init(vo, window_, error)) return false;
  if (!compositor_.Init(&ctx_, error)) return false;
  const VkExtent2D e = compositor_.extent();
  overlay_.assign(size_t{e.width} * e.height * 4, 0);
  std::printf("bootviz: Vulkan device %s, %ux%u%s\n", ctx_.device_name().c_str(), e.width,
              e.height, opt_.headless ? " (headless)" : "");
  std::fflush(stdout);
  return true;
}

void App::Shutdown() {
  compositor_.Shutdown();
  ctx_.Shutdown();
  if (window_) {
    glfwDestroyWindow(window_);
    glfwTerminate();
    window_ = nullptr;
  }
}

void App::Seek(float seconds) {
  boot_time_ = std::clamp(seconds, 0.f, scene::TotalDuration());
  hold_ = 0.f;
}

bool App::RenderFrame(float wall_time) {
  const VkExtent2D e = compositor_.extent();
  if (e.width == 0 || e.height == 0) return true;
  scene::FrameModel m;
  m.width = static_cast<int>(e.width);
  m.height = static_cast<int>(e.height);
  m.cursor = scene::CursorAt(boot_time_);
  m.time = wall_time;
  m.speed = opt_.speed;
  m.paused = paused_;
  m.show_keys = !opt_.headless;
  scene_->Render(m, overlay_.data());

  const scene::Stage& st = scene::BootStages()[m.cursor.stage];
  render::FrameInput in;
  in.backdrop.time = wall_time;
  in.backdrop.progress = boot_time_ / scene::TotalDuration();
  in.backdrop.accent = {SkColorGetR(st.accent) / 255.f, SkColorGetG(st.accent) / 255.f,
                        SkColorGetB(st.accent) / 255.f, 1.f};
  in.overlay = overlay_.data();
  in.overlay_version = ++overlay_version_;
  return compositor_.DrawFrame(in) == render::Compositor::FrameResult::kOk;
}

int App::SaveScreenshot(const std::string& path) {
  std::vector<uint8_t> rgba;
  if (!compositor_.ReadPixels(&rgba)) {
    std::fprintf(stderr, "bootviz: screenshot readback failed\n");
    return 1;
  }
  const VkExtent2D e = compositor_.extent();
  if (!stbi_write_png(path.c_str(), static_cast<int>(e.width), static_cast<int>(e.height), 4,
                      rgba.data(), static_cast<int>(e.width) * 4)) {
    std::fprintf(stderr, "bootviz: cannot write %s\n", path.c_str());
    return 1;
  }
  std::printf("wrote %s (%ux%u)\n", path.c_str(), e.width, e.height);
  return 0;
}

void App::OnKey(int key, int action) {
  if (action == GLFW_RELEASE) return;
  const scene::Cursor cur = scene::CursorAt(boot_time_);
  const int nstages = static_cast<int>(scene::BootStages().size());
  switch (key) {
    case GLFW_KEY_ESCAPE:
    case GLFW_KEY_Q:
      glfwSetWindowShouldClose(window_, GLFW_TRUE);
      break;
    case GLFW_KEY_SPACE:
      paused_ = !paused_;
      break;
    case GLFW_KEY_RIGHT:
      Seek(scene::SecondsAt({std::min(nstages - 1, cur.stage + 1), 0.f}));
      break;
    case GLFW_KEY_LEFT:
      // Back to the start of this stage, or the previous one if already there.
      Seek(scene::SecondsAt({cur.progress > 0.1f ? cur.stage : std::max(0, cur.stage - 1), 0.f}));
      break;
    case GLFW_KEY_R:
    case GLFW_KEY_HOME:
      Seek(0.f);
      break;
    case GLFW_KEY_EQUAL:
    case GLFW_KEY_KP_ADD:
      opt_.speed = std::min(8.f, opt_.speed * 2.f);
      break;
    case GLFW_KEY_MINUS:
    case GLFW_KEY_KP_SUBTRACT:
      opt_.speed = std::max(0.125f, opt_.speed / 2.f);
      break;
    default:
      if (key >= GLFW_KEY_1 && key < GLFW_KEY_1 + nstages) {
        Seek(scene::SecondsAt({key - GLFW_KEY_1, 0.f}));
      }
  }
}

void App::OnResize() {
  resize_pending_ = false;
  int w = 0, h = 0;
  glfwGetFramebufferSize(window_, &w, &h);
  while (w == 0 || h == 0) {  // minimised
    glfwWaitEvents();
    glfwGetFramebufferSize(window_, &w, &h);
  }
  std::string error;
  if (!compositor_.Resize(w, h, &error)) {
    std::fprintf(stderr, "bootviz: resize failed: %s\n", error.c_str());
    return;
  }
  const VkExtent2D e = compositor_.extent();
  overlay_.assign(size_t{e.width} * e.height * 4, 0);
}

}  // namespace bootviz::app
