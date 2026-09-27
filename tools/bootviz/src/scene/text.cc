#include "src/scene/text.h"

#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "include/ports/SkFontMgr_directory.h"

namespace bootviz::scene {

bool Fonts::Load(const std::string& font_dir, std::string* error) {
  mgr_ = SkFontMgr_New_Custom_Directory(font_dir.c_str());
  if (!mgr_ || mgr_->countFamilies() == 0) {
    *error = "no fonts found under " + font_dir + " (pass --font_dir)";
    return false;
  }
  for (const char* family : {"DejaVu Sans", "Noto Sans", "Liberation Sans", "Cantarell",
                             "Ubuntu", "FreeSans"}) {
    if (sk_sp<SkTypeface> tf = mgr_->matchFamilyStyle(family, SkFontStyle::Normal())) {
      regular_ = tf;
      bold_ = mgr_->matchFamilyStyle(family, SkFontStyle::Bold());
      break;
    }
  }
  for (const char* family : {"DejaVu Sans Mono", "Noto Sans Mono", "Liberation Mono",
                             "Ubuntu Mono", "FreeMono"}) {
    if (sk_sp<SkTypeface> tf = mgr_->matchFamilyStyle(family, SkFontStyle::Normal())) {
      mono_ = tf;
      break;
    }
  }
  if (!regular_) regular_ = mgr_->legacyMakeTypeface(nullptr, SkFontStyle::Normal());
  if (!regular_) {
    *error = "no usable font under " + font_dir + " (install fonts-dejavu-core)";
    return false;
  }
  if (!mono_) mono_ = regular_;
  return true;
}

SkFont Fonts::Make(const sk_sp<SkTypeface>& tf, float size, bool embolden) {
  SkFont f(tf, size);
  f.setEdging(SkFont::Edging::kAntiAlias);
  f.setSubpixel(true);
  f.setEmbolden(embolden);
  return f;
}

SkFont Fonts::Regular(float size) const { return Make(regular_, size, false); }
SkFont Fonts::Bold(float size) const { return Make(bold_ ? bold_ : regular_, size, !bold_); }
SkFont Fonts::Mono(float size) const { return Make(mono_, size, false); }

float TextWidth(const SkFont& font, std::string_view text) {
  return font.measureText(text.data(), text.size(), SkTextEncoding::kUTF8);
}

float DrawText(SkCanvas* c, std::string_view text, float x, float y, const SkFont& font,
               SkColor color, Align align) {
  const float w = TextWidth(font, text);
  if (align == Align::kCenter) x -= w * 0.5f;
  else if (align == Align::kRight) x -= w;
  SkPaint p;
  p.setAntiAlias(true);
  p.setColor(color);
  c->drawSimpleText(text.data(), text.size(), SkTextEncoding::kUTF8, x, y, font, p);
  return w;
}

std::vector<std::string> Wrap(const SkFont& font, std::string_view text, float width) {
  std::vector<std::string> lines;
  std::string line;
  size_t i = 0;
  while (i < text.size()) {
    size_t j = text.find(' ', i);
    if (j == std::string_view::npos) j = text.size();
    std::string word(text.substr(i, j - i));
    std::string candidate = line.empty() ? word : line + " " + word;
    if (!line.empty() && TextWidth(font, candidate) > width) {
      lines.push_back(line);
      line = word;
    } else {
      line = candidate;
    }
    i = j + 1;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

std::string Ellipsize(const SkFont& font, std::string_view text, float max_width) {
  if (TextWidth(font, text) <= max_width) return std::string(text);
  static const std::string kEllipsis = "…";
  std::string s(text);
  while (!s.empty()) {
    size_t cut = s.size() - 1;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    s.resize(cut);
    if (TextWidth(font, s + kEllipsis) <= max_width) return s + kEllipsis;
  }
  return kEllipsis;
}

}  // namespace bootviz::scene
