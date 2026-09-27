// Fonts and text helpers on top of Skia (after twn_election's src/ui/text.h).
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"

namespace bootviz::scene {

class Fonts {
 public:
  // Scans `font_dir` recursively; needs a sans-serif and a monospace face
  // (DejaVu, Noto, Liberation, ... in that order of preference).
  bool Load(const std::string& font_dir, std::string* error);

  SkFont Regular(float size) const;
  SkFont Bold(float size) const;
  SkFont Mono(float size) const;

 private:
  static SkFont Make(const sk_sp<SkTypeface>& tf, float size, bool embolden);
  sk_sp<SkFontMgr> mgr_;
  sk_sp<SkTypeface> regular_, bold_, mono_;
};

enum class Align { kLeft, kCenter, kRight };

float TextWidth(const SkFont& font, std::string_view text);

// Draws UTF-8 text with its baseline at y; returns the advance.
float DrawText(SkCanvas* c, std::string_view text, float x, float y, const SkFont& font,
               SkColor color, Align align = Align::kLeft);

// Greedy word wrap into lines no wider than `width`.
std::vector<std::string> Wrap(const SkFont& font, std::string_view text, float width);

// Shortens `text` with "…" to fit `max_width`.
std::string Ellipsize(const SkFont& font, std::string_view text, float max_width);

}  // namespace bootviz::scene
