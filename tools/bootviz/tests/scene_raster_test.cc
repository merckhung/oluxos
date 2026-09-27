// Renders every stage with the Skia CPU backend (no Vulkan needed) and
// checks that each frame draws something and differs from the others.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/scene/scene.h"

int main() {
  using namespace bootviz::scene;
  const char* dir = std::getenv("BOOTVIZ_FONT_DIR");
  Fonts fonts;
  std::string error;
  if (!fonts.Load(dir ? dir : "/usr/share/fonts", &error)) {
    std::printf("scene_raster_test: skipped (%s)\n", error.c_str());
    return 0;
  }
  Scene scene(&fonts);
  const int w = 800, h = 450;
  std::vector<std::vector<uint8_t>> frames;
  for (int i = 0; i < static_cast<int>(BootStages().size()); ++i) {
    FrameModel m;
    m.width = w;
    m.height = h;
    m.cursor = {i, BootStages()[i].showcase};
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    scene.Render(m, px.data());
    size_t opaque = 0;
    for (size_t k = 3; k < px.size(); k += 4) opaque += px[k] > 0;
    // Panels cover most of the frame; the backdrop shows through the rest.
    if (opaque < px.size() / 4 / 2) {
      std::fprintf(stderr, "FAIL: stage %d drew too little (%zu px)\n", i + 1, opaque);
      return 1;
    }
    for (const auto& f : frames) {
      if (f == px) {
        std::fprintf(stderr, "FAIL: stage %d looks like an earlier stage\n", i + 1);
        return 1;
      }
    }
    frames.push_back(std::move(px));
  }
  std::printf("scene_raster_test: %zu stages rendered\n", frames.size());
  return 0;
}
