#include "src/scene/scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "include/core/SkBlurTypes.h"
#include "include/core/SkColor.h"
#include "include/core/SkContourMeasure.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"

namespace bootviz::scene {
namespace {

// Palette (ARGB).
constexpr SkColor kPanel = 0xD90B1620;
constexpr SkColor kPanelBorder = 0xFF1F3444;
constexpr SkColor kText = 0xFFE6EEF5;
constexpr SkColor kMuted = 0xFF8FA3B5;
constexpr SkColor kFaint = 0xFF4E6272;
constexpr SkColor kBlock = 0xE6122230;
constexpr SkColor kBlockDim = 0xB30E1A24;
constexpr SkColor kTrace = 0xFF22394A;

SkColor WithAlpha(SkColor c, float a) {
  return SkColorSetA(c, static_cast<U8CPU>(std::clamp(a, 0.f, 1.f) * 255.f));
}

SkColor Mix(SkColor a, SkColor b, float t) {
  t = std::clamp(t, 0.f, 1.f);
  auto ch = [t](unsigned x, unsigned y) {
    return static_cast<U8CPU>(std::lround(x + (static_cast<float>(y) - x) * t));
  };
  return SkColorSetARGB(ch(SkColorGetA(a), SkColorGetA(b)), ch(SkColorGetR(a), SkColorGetR(b)),
                        ch(SkColorGetG(a), SkColorGetG(b)), ch(SkColorGetB(a), SkColorGetB(b)));
}

SkPaint Fill(SkColor c) {
  SkPaint p;
  p.setAntiAlias(true);
  p.setColor(c);
  return p;
}

SkPaint Stroke(SkColor c, float width) {
  SkPaint p = Fill(c);
  p.setStyle(SkPaint::kStroke_Style);
  p.setStrokeWidth(width);
  return p;
}

void Panel(SkCanvas* c, SkRect r, float radius = 12.f) {
  c->drawRRect(SkRRect::MakeRectXY(r, radius, radius), Fill(kPanel));
  c->drawRRect(SkRRect::MakeRectXY(r.makeInset(0.5f, 0.5f), radius, radius),
               Stroke(kPanelBorder, 1.f));
}

void Glow(SkCanvas* c, SkRect r, float radius, SkColor color, float sigma) {
  SkPaint p = Fill(color);
  p.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma));
  c->drawRRect(SkRRect::MakeRectXY(r, radius, radius), p);
}

float Smooth(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}

// ---------------------------------------------------------------------------
// Board layout (design coordinates, 1600x900)
// ---------------------------------------------------------------------------

struct Block {
  SkRect r;
  const char* label;
  const char* sub;
};

const std::map<Part, Block>& Blocks() {
  static const std::map<Part, Block> blocks = {
      {Part::kEeprom, {SkRect::MakeLTRB(52, 196, 206, 256), "SPI EEPROM", "bootloader"}},
      {Part::kSdAuto, {SkRect::MakeLTRB(64, 330, 196, 366), "p1 OLUXAUTO", "autoboot.txt"}},
      {Part::kSdBootA, {SkRect::MakeLTRB(64, 372, 196, 414), "p2 OLUXBOOTA", "slot A"}},
      {Part::kSdBootB, {SkRect::MakeLTRB(64, 420, 196, 462), "p3 OLUXBOOTB", "slot B"}},
      {Part::kSdData, {SkRect::MakeLTRB(64, 468, 196, 516), "p4 OLUXDATA", "/data"}},
      {Part::kVpu, {SkRect::MakeLTRB(272, 212, 476, 336), "VideoCore VI", "VPU + GPU"}},
      {Part::kRom, {SkRect::MakeLTRB(288, 270, 380, 322), "Boot ROM", nullptr}},
      {Part::kMailbox, {SkRect::MakeLTRB(490, 246, 562, 300), "Mailbox", nullptr}},
      {Part::kCpu0, {SkRect::MakeLTRB(588, 242, 680, 302), "Core 0", nullptr}},
      {Part::kCpu1, {SkRect::MakeLTRB(690, 242, 782, 302), "Core 1", nullptr}},
      {Part::kCpu2, {SkRect::MakeLTRB(588, 312, 680, 372), "Core 2", nullptr}},
      {Part::kCpu3, {SkRect::MakeLTRB(690, 312, 782, 372), "Core 3", nullptr}},
      {Part::kGic, {SkRect::MakeLTRB(588, 390, 680, 430), "GIC-400", nullptr}},
      {Part::kTimer, {SkRect::MakeLTRB(690, 390, 782, 430), "Arch timer", nullptr}},
      {Part::kEmmc2, {SkRect::MakeLTRB(272, 470, 352, 530), "EMMC2", "SD host"}},
      {Part::kGpio, {SkRect::MakeLTRB(360, 470, 440, 530), "GPIO", "pinmux"}},
      {Part::kPm, {SkRect::MakeLTRB(448, 470, 528, 530), "PM", "watchdog"}},
      {Part::kUart, {SkRect::MakeLTRB(536, 470, 616, 530), "PL011", "UART0"}},
      {Part::kPcie, {SkRect::MakeLTRB(624, 470, 704, 530), "PCIe", "brcmstb"}},
      {Part::kGenet, {SkRect::MakeLTRB(712, 470, 792, 530), "GENET", "Ethernet"}},
      {Part::kRam, {SkRect::MakeLTRB(318, 580, 746, 626), "LPDDR4 SDRAM", "2 GiB"}},
      {Part::kVl805, {SkRect::MakeLTRB(838, 330, 984, 392), "VL805", "USB 3 host"}},
      {Part::kPhy, {SkRect::MakeLTRB(838, 444, 984, 504), "BCM54213PE", "PHY + RJ45"}},
      {Part::kSerial, {SkRect::MakeLTRB(838, 560, 984, 624), "Serial terminal", "GPIO 14/15"}},
  };
  return blocks;
}

const SkRect kSoc = SkRect::MakeLTRB(256, 186, 800, 546);
const SkRect kSdCard = SkRect::MakeLTRB(48, 296, 212, 530);
const SkRect kArmCluster = SkRect::MakeLTRB(578, 212, 792, 440);

SkPoint Center(Part p) {
  const SkRect& r = Blocks().at(p).r;
  return {r.centerX(), r.centerY()};
}

// Point on the edge of `r` facing `toward`.
SkPoint EdgeToward(const SkRect& r, SkPoint toward) {
  const SkPoint c{r.centerX(), r.centerY()};
  const float dx = toward.fX - c.fX, dy = toward.fY - c.fY;
  if (std::fabs(dx) * r.height() > std::fabs(dy) * r.width()) {
    return {dx > 0 ? r.fRight : r.fLeft, c.fY};
  }
  return {c.fX, dy > 0 ? r.fBottom : r.fTop};
}

// Hand-routed wires (orthogonal, through the gaps between blocks). A route
// listed as (a, b) also serves (b, a), reversed.
struct Wire {
  Part a, b;
  std::vector<SkPoint> pts;
};

const std::vector<Wire>& Wires() {
  using P = Part;
  static const std::vector<Wire> wires = {
      {P::kEeprom, P::kVpu, {{206, 226}, {272, 226}}},
      {P::kSdAuto, P::kVpu, {{196, 348}, {226, 348}, {226, 282}, {272, 282}}},
      {P::kSdBootA, P::kVpu, {{196, 393}, {236, 393}, {236, 304}, {272, 304}}},
      {P::kSdBootA, P::kEmmc2, {{196, 393}, {236, 393}, {236, 506}, {272, 506}}},
      {P::kSdBootA, P::kRam, {{196, 400}, {242, 400}, {242, 603}, {318, 603}}},
      {P::kEmmc2, P::kSdData, {{272, 494}, {236, 494}, {236, 492}, {196, 492}}},
      {P::kEmmc2, P::kRam, {{312, 530}, {312, 560}, {340, 560}, {340, 580}}},
      {P::kVpu, P::kRam, {{444, 336}, {444, 580}}},
      {P::kVpu, P::kMailbox, {{476, 262}, {490, 262}}},
      {P::kMailbox, P::kCpu0, {{562, 262}, {588, 262}}},
      {P::kVpu, P::kCpu0, {{476, 322}, {570, 322}, {570, 292}, {588, 292}}},
      {P::kRam, P::kCpu0, {{620, 580}, {620, 462}, {572, 462}, {572, 282}, {588, 282}}},
      {P::kCpu0, P::kUart, {{588, 292}, {568, 292}, {568, 462}, {576, 462}, {576, 470}}},
      {P::kCpu0, P::kGic, {{588, 286}, {574, 286}, {574, 410}, {588, 410}}},
      {P::kTimer, P::kCpu0, {{782, 410}, {788, 410}, {788, 236}, {634, 236}, {634, 242}}},
      {P::kCpu0, P::kGpio, {{588, 296}, {566, 296}, {566, 462}, {400, 462}, {400, 470}}},
      {P::kCpu0, P::kPcie, {{588, 298}, {570, 298}, {570, 462}, {664, 462}, {664, 470}}},
      {P::kCpu0, P::kCpu1, {{680, 272}, {690, 272}}},
      {P::kCpu0, P::kCpu2, {{634, 302}, {634, 312}}},
      {P::kCpu0, P::kCpu3, {{680, 296}, {685, 296}, {685, 342}, {690, 342}}},
      {P::kCpu1, P::kPm, {{782, 272}, {788, 272}, {788, 462}, {488, 462}, {488, 470}}},
      {P::kSdData, P::kCpu2, {{196, 504}, {248, 504}, {248, 462}, {570, 462}, {570, 342}, {588, 342}}},
      {P::kSdBootA, P::kCpu3, {{196, 386}, {246, 386}, {246, 460}, {788, 460}, {788, 342}, {782, 342}}},
      {P::kUart, P::kSerial, {{576, 530}, {576, 556}, {812, 556}, {812, 592}, {838, 592}}},
      {P::kPcie, P::kVl805, {{664, 470}, {664, 464}, {810, 464}, {810, 361}, {838, 361}}},
      {P::kGenet, P::kPhy, {{792, 500}, {815, 500}, {815, 474}, {838, 474}}},
  };
  return wires;
}

// An orthogonal route between two blocks: out of `a` toward `b`, one bend.
SkPath AutoRoute(Part a, Part b) {
  const SkRect& ra = Blocks().at(a).r;
  const SkRect& rb = Blocks().at(b).r;
  const SkPoint ca = Center(a), cb = Center(b);
  const SkPoint p0 = EdgeToward(ra, cb);
  const SkPoint p1 = EdgeToward(rb, ca);
  SkPathBuilder pb;
  pb.moveTo(p0);
  const bool horizontal_first = p0.fX == ra.fLeft || p0.fX == ra.fRight;
  if (horizontal_first) {
    const float midx = (p0.fX + p1.fX) * 0.5f;
    if (p1.fY == rb.fTop || p1.fY == rb.fBottom) {
      pb.lineTo(p1.fX, p0.fY);
    } else {
      pb.lineTo(midx, p0.fY);
      pb.lineTo(midx, p1.fY);
    }
  } else {
    if (p1.fX == rb.fLeft || p1.fX == rb.fRight) {
      pb.lineTo(p0.fX, p1.fY);
    } else {
      const float midy = (p0.fY + p1.fY) * 0.5f;
      pb.lineTo(p0.fX, midy);
      pb.lineTo(p1.fX, midy);
    }
  }
  pb.lineTo(p1);
  return pb.detach();
}

SkPath Route(Part a, Part b) {
  for (const Wire& w : Wires()) {
    const bool fwd = w.a == a && w.b == b, rev = w.a == b && w.b == a;
    if (!fwd && !rev) continue;
    SkPathBuilder pb;
    if (fwd) {
      pb.moveTo(w.pts.front());
      for (size_t i = 1; i < w.pts.size(); ++i) pb.lineTo(w.pts[i]);
    } else {
      pb.moveTo(w.pts.back());
      for (size_t i = w.pts.size() - 1; i-- > 0;) pb.lineTo(w.pts[i]);
    }
    return pb.detach();
  }
  return AutoRoute(a, b);
}

const char* CpuLabel(CpuState s) {
  switch (s) {
    case CpuState::kReset: return "RESET";
    case CpuState::kSpin: return "SPIN · WFE";
    case CpuState::kRun: return "RUN";
    case CpuState::kIdle: return "IDLE · WFI";
  }
  return "";
}

SkColor CpuColor(CpuState s) {
  switch (s) {
    case CpuState::kReset: return 0xFF5B6B78;
    case CpuState::kSpin: return 0xFFFFB547;
    case CpuState::kRun: return 0xFF6BE38A;
    case CpuState::kIdle: return 0xFF7FB2FF;
  }
  return kMuted;
}

// CPU state at the cursor: each stage's table, except that the kernel_init
// stage releases cores 1-3 part-way through (when its "release" flows land).
CpuState CpuAt(const Stage& st, int cpu, float progress) {
  if (cpu > 0 && st.cpus[cpu] == CpuState::kSpin) {
    for (const Flow& f : st.flows) {
      if (f.from == Part::kCpu0 && static_cast<int>(f.to) == static_cast<int>(Part::kCpu0) + cpu &&
          progress >= f.end) {
        return CpuState::kRun;
      }
    }
  }
  return st.cpus[cpu];
}

std::string Hex(uint64_t v) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "0x%08llx", static_cast<unsigned long long>(v));
  return buf;
}

std::string Size(uint64_t v) {
  char buf[24];
  if (v >= (1ull << 20)) std::snprintf(buf, sizeof buf, "%.1f MiB", v / 1048576.0);
  else std::snprintf(buf, sizeof buf, "%llu KiB", static_cast<unsigned long long>(v >> 10));
  std::string s = buf;
  if (s.size() > 6 && s.compare(s.size() - 6, 6, ".0 MiB") == 0) s.erase(s.size() - 6, 2);
  return s;
}

}  // namespace

// ---------------------------------------------------------------------------

void Scene::Render(const FrameModel& m, uint8_t* pixels) {
  const SkImageInfo info = SkImageInfo::MakeN32Premul(m.width, m.height);
  std::unique_ptr<SkCanvas> canvas =
      SkCanvas::MakeRasterDirect(info, pixels, static_cast<size_t>(m.width) * 4);
  SkCanvas* c = canvas.get();
  c->clear(SK_ColorTRANSPARENT);
  const float s = std::min(m.width / 1600.f, m.height / 900.f);
  c->translate((m.width - 1600.f * s) * 0.5f, (m.height - 900.f * s) * 0.5f);
  c->scale(s, s);
  DrawHeader(c, m);
  DrawRail(c, m);
  DrawBoard(c, m);
  DrawInfo(c, m);
  DrawMemory(c, m);
  DrawConsole(c, m);
}

void Scene::DrawHeader(SkCanvas* c, const FrameModel& m) {
  const Stage& st = BootStages()[m.cursor.stage];
  c->drawRect(SkRect::MakeLTRB(-400, -400, 2000, 132), Fill(0x99060D13));
  c->drawLine(-400, 132, 2000, 132, Stroke(0x401F3444, 1.f));
  // Logo mark: a small chip.
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(28, 18, 30, 30), 6, 6), Fill(st.accent));
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(35, 25, 16, 16), 3, 3), Fill(0xFF0B1620));
  const float tw = DrawText(c, "How OluxOS boots on a Raspberry Pi 4", 72, 42, fonts_->Bold(26),
                            kText);
  DrawText(c, "from power-on to a shell: firmware, kernel, init", 72 + tw + 16, 42,
           fonts_->Regular(15), kMuted);

  char right[96];
  std::snprintf(right, sizeof right, "t = %5.1f s / %.0f s   %s %.1fx", SecondsAt(m.cursor),
                TotalDuration(), m.paused ? "paused" : "playing", m.speed);
  DrawText(c, right, 1572, 32, fonts_->Mono(14), kMuted, Align::kRight);
  if (m.show_keys) {
    DrawText(c, "Space pause · ←/→ stage · 1-8 jump · +/− speed · R restart", 1572, 52,
             fonts_->Regular(12.5f), kFaint, Align::kRight);
  }
}

void Scene::DrawRail(SkCanvas* c, const FrameModel& m) {
  const auto& stages = BootStages();
  const int n = static_cast<int>(stages.size());
  const float x0 = 60, x1 = 1540, y = 92;
  const float step = (x1 - x0) / (n - 1);
  c->drawLine(x0, y, x1, y, Stroke(kTrace, 4));
  const float done_x = x0 + step * (m.cursor.stage + m.cursor.progress);
  SkPaint prog = Stroke(stages[m.cursor.stage].accent, 4);
  prog.setStrokeCap(SkPaint::kRound_Cap);
  c->drawLine(x0, y, std::min(done_x, x1), y, prog);
  for (int i = 0; i < n; ++i) {
    const float x = x0 + step * i;
    const bool current = i == m.cursor.stage;
    const bool done = i < m.cursor.stage;
    const SkColor col = current || done ? stages[i].accent : kFaint;
    if (current) {
      const float pulse = 0.5f + 0.5f * std::sin(m.time * 4.f);
      c->drawCircle(x, y, 17 + 3 * pulse, Fill(WithAlpha(col, 0.18f)));
    }
    c->drawCircle(x, y, 11, Fill(done ? col : 0xFF0B1620));
    c->drawCircle(x, y, 11, Stroke(col, 2.5f));
    char num[12];
    std::snprintf(num, sizeof num, "%d", i + 1);
    DrawText(c, num, x, y + 4.5f, fonts_->Bold(12), done ? 0xFF0B1620 : col, Align::kCenter);
    DrawText(c, stages[i].name, x, y + 34, current ? fonts_->Bold(14) : fonts_->Regular(13.5f),
             current ? kText : (done ? kMuted : kFaint), Align::kCenter);
  }
}

void Scene::DrawBoard(SkCanvas* c, const FrameModel& m) {
  const Stage& st = BootStages()[m.cursor.stage];
  const float p = m.cursor.progress;
  Panel(c, SkRect::MakeLTRB(24, 140, 1004, 644));
  DrawText(c, "Raspberry Pi 4 Model B", 44, 170, fonts_->Bold(16), kText);
  DrawText(c, "blocks involved in this stage are lit; moving dots are data or control transfers",
           256, 170, fonts_->Regular(12.5f), kFaint);

  auto is_active = [&](Part part) {
    return std::find(st.active.begin(), st.active.end(), part) != st.active.end();
  };

  // Static wiring (dim), drawn under the blocks.
  static const std::pair<Part, Part> kWires[] = {
      {Part::kEeprom, Part::kVpu},   {Part::kSdBootA, Part::kEmmc2}, {Part::kEmmc2, Part::kSdData},
      {Part::kVpu, Part::kMailbox},  {Part::kMailbox, Part::kCpu0},  {Part::kUart, Part::kSerial},
      {Part::kPcie, Part::kVl805},   {Part::kGenet, Part::kPhy},     {Part::kEmmc2, Part::kRam},
      {Part::kCpu0, Part::kGic},     {Part::kVpu, Part::kRam},
  };
  for (const auto& [a, b] : kWires) c->drawPath(Route(a, b), Stroke(kTrace, 2));
  // Interconnect bar inside the SoC.
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeLTRB(272, 448, 792, 456), 3, 3), Fill(kTrace));
  DrawText(c, "AXI interconnect", 276, 466, fonts_->Regular(10), kFaint);

  // SoC and the SD card outlines.
  c->drawRRect(SkRRect::MakeRectXY(kSoc, 14, 14), Fill(0x990A1520));
  c->drawRRect(SkRRect::MakeRectXY(kSoc, 14, 14), Stroke(0xFF2C4A5E, 1.5f));
  DrawText(c, "BCM2711 SoC", kSoc.fRight - 12, kSoc.fTop + 19, fonts_->Bold(12.5f), kMuted,
           Align::kRight);
  c->drawRRect(SkRRect::MakeRectXY(kArmCluster, 10, 10), Fill(0x66102433));
  DrawText(c, "4 × Cortex-A72", kArmCluster.fLeft + 10, kArmCluster.fTop + 20,
           fonts_->Regular(12.5f), kMuted);
  {
    SkPathBuilder card;
    card.moveTo(kSdCard.fLeft + 26, kSdCard.fTop);
    card.lineTo(kSdCard.fRight - 8, kSdCard.fTop);
    card.lineTo(kSdCard.fRight, kSdCard.fTop + 8);
    card.lineTo(kSdCard.fRight, kSdCard.fBottom);
    card.lineTo(kSdCard.fLeft, kSdCard.fBottom);
    card.lineTo(kSdCard.fLeft, kSdCard.fTop + 26);
    card.close();
    SkPath path = card.detach();
    c->drawPath(path, Fill(0xCC0E1B26));
    c->drawPath(path, Stroke(0xFF2C4A5E, 1.5f));
    DrawText(c, "microSD card", kSdCard.fLeft + 30, kSdCard.fTop + 22, fonts_->Bold(12.5f),
             kMuted);
  }

  // Blocks.
  std::vector<Part> order;
  order.push_back(Part::kVpu);
  for (const auto& [part, b] : Blocks()) {
    if (part != Part::kVpu) order.push_back(part);
  }
  for (Part part : order) {
    const Block& b = Blocks().at(part);
    const bool active = is_active(part);
    const bool is_cpu = part >= Part::kCpu0 && part <= Part::kCpu3;
    SkColor border = active ? st.accent : 0xFF2A3E4E;
    SkColor fill = active ? kBlock : kBlockDim;
    CpuState cs = CpuState::kReset;
    if (is_cpu) {
      cs = CpuAt(st, static_cast<int>(part) - static_cast<int>(Part::kCpu0), p);
      border = CpuColor(cs);
    }
    if (part == Part::kSdBootA || part == Part::kSdBootB) {
      const int slot = part == Part::kSdBootA ? 2 : 3;
      if (st.sd_slot == slot) border = st.accent;
    }
    if (active || (is_cpu && cs == CpuState::kRun)) {
      Glow(c, b.r.makeOutset(2, 2), 8, WithAlpha(border, 0.45f), 7);
    }
    c->drawRRect(SkRRect::MakeRectXY(b.r, 7, 7), Fill(fill));
    c->drawRRect(SkRRect::MakeRectXY(b.r.makeInset(0.75f, 0.75f), 7, 7),
                 Stroke(border, active ? 2.f : 1.2f));
    const SkColor label_col = active || (is_cpu && cs != CpuState::kReset) ? kText : kMuted;
    if (part == Part::kVpu) {
      DrawText(c, b.label, b.r.fLeft + 12, b.r.fTop + 22, fonts_->Bold(14), label_col);
      DrawText(c, b.sub, b.r.fLeft + 12, b.r.fTop + 40, fonts_->Regular(11.5f), kMuted);
      continue;
    }
    if (is_cpu) {
      DrawText(c, b.label, b.r.centerX(), b.r.fTop + 24, fonts_->Bold(13.5f), label_col,
               Align::kCenter);
      DrawText(c, CpuLabel(cs), b.r.centerX(), b.r.fTop + 44, fonts_->Mono(11.5f), CpuColor(cs),
               Align::kCenter);
      continue;
    }
    const bool small = b.r.height() < 50;
    const float cy = b.sub ? b.r.centerY() - (small ? 1 : 3) : b.r.centerY() + 5;
    DrawText(c, b.label, b.r.centerX(), cy, fonts_->Bold(small ? 11.5f : 12.5f), label_col,
             Align::kCenter);
    if (b.sub) {
      DrawText(c, b.sub, b.r.centerX(), cy + (small ? 13 : 16), fonts_->Regular(10.5f), kMuted,
               Align::kCenter);
    }
  }
  // "boot" tag on the selected slot.
  if (st.sd_slot == 2 || st.sd_slot == 3) {
    const SkRect& r = Blocks().at(st.sd_slot == 2 ? Part::kSdBootA : Part::kSdBootB).r;
    SkRect tag = SkRect::MakeXYWH(r.fRight - 6, r.fTop - 8, 42, 18);
    c->drawRRect(SkRRect::MakeRectXY(tag, 9, 9), Fill(st.accent));
    DrawText(c, "boot", tag.centerX(), tag.fBottom - 5, fonts_->Bold(11), 0xFF0B1620,
             Align::kCenter);
  }

  // Active flows: a lit trace with a stream of packets and a label.
  for (const Flow& f : st.flows) {
    if (p < f.start) continue;
    const float local = f.end > f.start ? (p - f.start) / (f.end - f.start) : 1.f;
    const bool running = local <= 1.f;
    const SkPath path = Route(f.from, f.to);
    SkContourMeasureIter iter(path, false);
    sk_sp<SkContourMeasure> cm = iter.next();
    if (!cm) continue;
    const float len = cm->length();
    const float fade = running ? 1.f : 0.35f;
    SkPaint lit = Stroke(WithAlpha(f.color, 0.55f * fade), 2.5f);
    lit.setStrokeCap(SkPaint::kRound_Cap);
    if (running) {
      // Draw the trace up to the packet head as the transfer starts.
      const float head = len * Smooth(std::min(1.f, local * 3.f));
      SkPathBuilder partial;
      if (cm->getSegment(0, head, &partial, true)) c->drawPath(partial.detach(), lit);
      const float spacing = 26.f;
      const float offset = std::fmod(m.time * 90.f, spacing);
      for (float d = offset; d < head; d += spacing) {
        SkPoint pos;
        SkVector tan;
        if (!cm->getPosTan(d, &pos, &tan)) continue;
        SkPaint dot = Fill(f.color);
        dot.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, 1.2f));
        c->drawCircle(pos.fX, pos.fY, 3.4f, dot);
      }
    } else {
      c->drawPath(path, lit);
    }
    if (!f.label.empty() && running) {
      SkPoint mid;
      SkVector tan;
      if (cm->getPosTan(len * f.label_at, &mid, &tan)) {
        const SkFont font = fonts_->Bold(11.5f);
        const float w = TextWidth(font, f.label) + 14;
        SkRect pill = SkRect::MakeXYWH(mid.fX - w / 2, mid.fY - 22, w, 18);
        if (std::fabs(tan.fY) > std::fabs(tan.fX)) pill.offset(f.label_side * (w / 2 + 8), 11);
        c->drawRRect(SkRRect::MakeRectXY(pill, 9, 9), Fill(0xF20B1620));
        c->drawRRect(SkRRect::MakeRectXY(pill, 9, 9), Stroke(f.color, 1.2f));
        DrawText(c, f.label, pill.centerX(), pill.fBottom - 5, font, f.color, Align::kCenter);
      }
    }
  }
}

void Scene::DrawInfo(SkCanvas* c, const FrameModel& m) {
  const auto& stages = BootStages();
  const Stage& st = stages[m.cursor.stage];
  const SkRect r = SkRect::MakeLTRB(1020, 140, 1576, 474);
  Panel(c, r);
  // Accent bar.
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeLTRB(r.fLeft, r.fTop + 14, r.fLeft + 4, r.fTop + 70),
                                   2, 2),
               Fill(st.accent));
  char tag[48];
  std::snprintf(tag, sizeof tag, "STAGE %d OF %d", m.cursor.stage + 1,
                static_cast<int>(stages.size()));
  DrawText(c, tag, r.fLeft + 22, r.fTop + 28, fonts_->Bold(12), st.accent);
  float y = r.fTop + 56;
  for (const std::string& line : Wrap(fonts_->Bold(21), st.title, r.width() - 44)) {
    DrawText(c, line, r.fLeft + 22, y, fonts_->Bold(21), kText);
    y += 26;
  }
  const float lw = DrawText(c, "Runs on:", r.fLeft + 22, y + 2, fonts_->Bold(12.5f), kMuted) + 6;
  for (const std::string& line :
       Wrap(fonts_->Regular(12.5f), st.actor, r.width() - 44 - lw)) {
    DrawText(c, line, r.fLeft + 22 + lw, y + 2, fonts_->Regular(12.5f), kMuted);
    y += 17;
  }
  y += 10;

  const int n = static_cast<int>(st.steps.size());
  const int current = std::min(n - 1, static_cast<int>(m.cursor.progress * n));
  // Largest body size whose wrapped steps fit above the source line.
  float size = 14.f, lh = 18.f;
  for (; size > 11.f; size -= 0.5f) {
    lh = size * 1.28f;
    float h = 0;
    for (const std::string& step : st.steps) {
      h += Wrap(fonts_->Regular(size), step, r.width() - 70).size() * lh + 6;
    }
    if (y + h <= r.fBottom - 34) break;
  }
  const SkFont body = fonts_->Regular(size);
  for (int i = 0; i < n; ++i) {
    const bool now = i == current;
    const SkColor col = i <= current ? kText : kMuted;
    const float bx = r.fLeft + 22;
    c->drawCircle(bx + 9, y - 5, 9, Fill(now ? st.accent : (i < current ? 0xFF2A4152 : 0xFF182A36)));
    char num[12];
    std::snprintf(num, sizeof num, "%d", i + 1);
    DrawText(c, num, bx + 9, y - 1, fonts_->Bold(11), now ? 0xFF0B1620 : kMuted, Align::kCenter);
    for (const std::string& line : Wrap(body, st.steps[i], r.width() - 70)) {
      DrawText(c, line, bx + 26, y, body, col);
      y += lh;
    }
    y += 6;
  }
  // Source reference.
  const SkFont mono = fonts_->Mono(12);
  const std::string where = Ellipsize(mono, st.where, r.width() - 110);
  DrawText(c, "Source:", r.fLeft + 22, r.fBottom - 16, fonts_->Bold(12), kMuted);
  DrawText(c, where, r.fLeft + 78, r.fBottom - 16, mono, st.accent);
}

void Scene::DrawMemory(SkCanvas* c, const FrameModel& m) {
  const Stage& st = BootStages()[m.cursor.stage];
  const SkRect r = SkRect::MakeLTRB(1020, 486, 1576, 644);
  Panel(c, r);
  const float mw =
      DrawText(c, "Physical memory", r.fLeft + 18, r.fTop + 24, fonts_->Bold(14.5f), kText);
  DrawText(c, "addresses from the recorded boot", r.fLeft + 18 + mw + 10, r.fTop + 24,
           fonts_->Regular(11.5f), kFaint);

  // A 1 GiB bar (the first bank) with the regions placed to scale, each at
  // least 3 px wide so the small ones stay visible.
  const SkRect bar = SkRect::MakeLTRB(r.fLeft + 18, r.fTop + 36, r.fRight - 18, r.fTop + 50);
  c->drawRRect(SkRRect::MakeRectXY(bar, 4, 4), Fill(0xFF12212C));
  const double gib = 1024.0 * 1024.0 * 1024.0;
  auto visible = [&](const MemRegion& mr) {
    return m.cursor.stage > mr.stage || (m.cursor.stage == mr.stage && m.cursor.progress >= mr.at);
  };
  std::vector<const MemRegion*> shown;
  for (const MemRegion& mr : MemoryMap()) {
    if (!visible(mr)) continue;
    shown.push_back(&mr);
    const float x0 = bar.fLeft + static_cast<float>(mr.base / gib) * bar.width();
    const float x1 = std::max(x0 + 3.f, bar.fLeft + static_cast<float>((mr.base + mr.size) / gib) *
                                            bar.width());
    const bool background = mr.size >= 0x10000000;
    c->drawRect(SkRect::MakeLTRB(x0, bar.fTop + (background ? 4 : 0), x1,
                                 bar.fBottom - (background ? 4 : 0)),
                Fill(WithAlpha(mr.color, background ? 0.35f : 0.95f)));
  }
  DrawText(c, "0", bar.fLeft, bar.fBottom + 12, fonts_->Mono(9.5f), kFaint);
  DrawText(c, "1 GiB", bar.fRight, bar.fBottom + 12, fonts_->Mono(9.5f), kFaint, Align::kRight);

  // Region list, newest last; the big "ARM RAM" row first.
  std::stable_sort(shown.begin(), shown.end(),
                   [](const MemRegion* a, const MemRegion* b) { return a->size > b->size; });
  float y = bar.fBottom + 30;
  const SkFont mono = fonts_->Mono(11.5f);
  const float row = 13.5f;
  if (shown.empty()) {
    DrawText(c, "(the ARM side has no memory map yet)", r.fLeft + 18, y, fonts_->Regular(12.5f),
             kFaint);
  }
  for (size_t i = 0; i < shown.size() && y < r.fBottom - 4; ++i) {
    const MemRegion& mr = *shown[i];
    const bool fresh = m.cursor.stage == mr.stage;
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(r.fLeft + 18, y - 9, 10, 10), 2, 2),
                 Fill(mr.color));
    const std::string range = Hex(mr.base) + "-" + Hex(mr.base + mr.size - 1);
    DrawText(c, range, r.fLeft + 36, y, mono, fresh ? kText : kMuted);
    DrawText(c, Size(mr.size), r.fLeft + 244, y, mono, kMuted);
    DrawText(c, mr.label, r.fLeft + 318, y, fonts_->Regular(12), fresh ? kText : kMuted);
    y += row;
  }
  (void)st;
}

void Scene::DrawConsole(SkCanvas* c, const FrameModel& m) {
  const Stage& st = BootStages()[m.cursor.stage];
  const SkRect r = SkRect::MakeLTRB(24, 658, 1576, 884);
  c->drawRRect(SkRRect::MakeRectXY(r, 12, 12), Fill(0xF2050B10));
  c->drawRRect(SkRRect::MakeRectXY(r.makeInset(0.5f, 0.5f), 12, 12), Stroke(kPanelBorder, 1.f));
  // Title bar.
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeLTRB(r.fLeft, r.fTop, r.fRight, r.fTop + 28), 12,
                                   12),
               Fill(0xFF0F1D28));
  c->drawRect(SkRect::MakeLTRB(r.fLeft, r.fTop + 16, r.fRight, r.fTop + 28), Fill(0xFF0F1D28));
  for (int i = 0; i < 3; ++i) {
    c->drawCircle(r.fLeft + 18 + i * 16, r.fTop + 14, 5,
                  Fill(i == 0 ? 0xFFFF6B6B : i == 1 ? 0xFFFFC75F : 0xFF6BE38A));
  }
  DrawText(c, "serial console · ttyAMA0 · 115200 8N1", r.fLeft + 72, r.fTop + 19,
           fonts_->Bold(12.5f), kMuted);
  DrawText(c,
           "kernel and init lines recorded from OluxOS 0.3.0 on QEMU raspi4b (make sdcard image)",
           r.fRight - 16, r.fTop + 19, fonts_->Regular(11.5f), kFaint, Align::kRight);

  const std::vector<std::string> lines = ConsoleUpTo(m.cursor);
  const SkFont mono = fonts_->Mono(13);
  const float line_h = 17.f;
  const float top = r.fTop + 50;
  const int max_lines = static_cast<int>((r.fBottom - 10 - top) / line_h) + 1;
  const int first = std::max(0, static_cast<int>(lines.size()) - max_lines);
  float y = top;
  const float width = r.width() - 32;
  for (int i = first; i < static_cast<int>(lines.size()); ++i) {
    const std::string& line = lines[i];
    SkColor col = 0xFFB9F6C8;
    if (line.rfind("root@", 0) == 0) col = 0xFFFFFFFF;
    else if (line.rfind("[", 0) != 0) col = 0xFFD7E3EC;
    // Newest lines fade in from the stage accent.
    const bool newest = i >= static_cast<int>(lines.size()) - 1;
    if (newest && line.rfind("root@", 0) != 0) col = Mix(st.accent, col, 0.35f);
    std::string text = line;
    const bool cursor_line = text.size() >= 2 && text.compare(text.size() - 2, 2, " _") == 0;
    if (cursor_line) text.resize(text.size() - 1);
    const std::string shown = Ellipsize(mono, text, width);
    const float adv = DrawText(c, shown, r.fLeft + 16, y, mono, col);
    if (cursor_line && std::fmod(m.time, 1.f) < 0.55f) {
      c->drawRect(SkRect::MakeXYWH(r.fLeft + 16 + adv, y - 12, 8, 15), Fill(0xFFE6EEF5));
    }
    y += line_h;
  }
  if (lines.empty() && !st.console_note.empty()) {
    DrawText(c, st.console_note, r.fLeft + 16, top, fonts_->Mono(13), kFaint);
  } else if (!st.console_note.empty() && st.log.empty()) {
    DrawText(c, st.console_note, r.fLeft + 16, std::min(y, r.fBottom - 12), fonts_->Mono(13),
             kFaint);
  }
}

}  // namespace bootviz::scene
