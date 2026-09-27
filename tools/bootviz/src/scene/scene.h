// Draws one frame of the boot explanation with Skia into a CPU pixel buffer
// (N32 premultiplied). Areas left transparent show the Vulkan backdrop.
//
// Layout is designed for 1600x900 and scaled uniformly to the target size.
#pragma once

#include <cstdint>

#include "src/scene/boot_script.h"
#include "src/scene/text.h"

namespace bootviz::scene {

struct FrameModel {
  int width = 1600, height = 900;
  Cursor cursor;
  float time = 0.f;   // wall-clock seconds, drives small animations (blinks)
  float speed = 1.f;  // playback speed, shown in the header
  bool paused = false;
  bool show_keys = true;  // the keyboard help line
};

class Scene {
 public:
  explicit Scene(const Fonts* fonts) : fonts_(fonts) {}

  // `pixels` is width * height * 4 bytes.
  void Render(const FrameModel& m, uint8_t* pixels);

 private:
  void DrawHeader(SkCanvas* c, const FrameModel& m);
  void DrawRail(SkCanvas* c, const FrameModel& m);
  void DrawBoard(SkCanvas* c, const FrameModel& m);
  void DrawInfo(SkCanvas* c, const FrameModel& m);
  void DrawMemory(SkCanvas* c, const FrameModel& m);
  void DrawConsole(SkCanvas* c, const FrameModel& m);

  const Fonts* fonts_;
};

}  // namespace bootviz::scene
