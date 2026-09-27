// bootviz: an animated 2D explanation of how OluxOS boots on a Raspberry Pi 4
// (Skia draws, Vulkan composites and presents).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "src/app/app.h"

namespace {

void Usage() {
  std::printf(
      "Usage: bootviz [flags]\n"
      "  --width=W --height=H   window or image size (default 1600x900)\n"
      "  --speed=X              playback speed (default 1; +/- change it at run time)\n"
      "  --stage=N              start at stage N (1-8)\n"
      "  --no_loop              stay on the last frame instead of starting over\n"
      "  --headless             render offscreen (no window; works with Mesa lavapipe)\n"
      "  --screenshot=FILE      headless: write stage --stage at --progress to a PNG\n"
      "  --progress=P           position within the stage, 0..1 (default: a moment\n"
      "                         chosen per stage to show its transfers)\n"
      "  --screenshots=DIR      headless: write one PNG per stage into DIR\n"
      "  --font_dir=DIR         where to look for fonts (default /usr/share/fonts)\n"
      "  --validation           enable VK_LAYER_KHRONOS_validation\n"
      "Keys: Space pause, Left/Right stage, 1-8 jump, +/- speed, R restart, Esc quit.\n");
}

bool Flag(const char* arg, const char* name, std::string* value) {
  const size_t n = std::strlen(name);
  if (std::strncmp(arg, name, n) != 0) return false;
  if (arg[n] == '=') {
    *value = arg + n + 1;
    return true;
  }
  if (arg[n] == '\0') {
    *value = "1";
    return true;
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  bootviz::app::AppOptions o;
  for (int i = 1; i < argc; ++i) {
    std::string v;
    const char* a = argv[i];
    if (Flag(a, "--width", &v)) o.width = std::atoi(v.c_str());
    else if (Flag(a, "--height", &v)) o.height = std::atoi(v.c_str());
    else if (Flag(a, "--speed", &v)) o.speed = static_cast<float>(std::atof(v.c_str()));
    else if (Flag(a, "--stage", &v)) o.stage = std::atoi(v.c_str());
    else if (Flag(a, "--progress", &v)) {
      o.progress = static_cast<float>(std::atof(v.c_str()));
      o.progress_set = true;
    }
    else if (Flag(a, "--no_loop", &v)) o.loop = v == "0";
    else if (Flag(a, "--headless", &v)) o.headless = v != "0";
    else if (Flag(a, "--screenshots", &v)) o.screenshots_dir = v;
    else if (Flag(a, "--screenshot", &v)) o.screenshot = v;
    else if (Flag(a, "--font_dir", &v)) o.font_dir = v;
    else if (Flag(a, "--validation", &v)) o.validation = v != "0";
    else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      Usage();
      return 0;
    } else {
      std::fprintf(stderr, "unknown flag: %s\n", a);
      Usage();
      return 2;
    }
  }
  if (o.width < 320 || o.height < 180) {
    std::fprintf(stderr, "--width/--height too small\n");
    return 2;
  }
  if (!o.screenshot.empty() || !o.screenshots_dir.empty()) o.headless = true;
  bootviz::app::App app;
  return app.Run(o);
}
