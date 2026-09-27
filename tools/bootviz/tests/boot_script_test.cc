// Consistency checks for the boot script (no test framework needed).
#include <cmath>
#include <cstdio>
#include <string>

#include "src/scene/boot_script.h"

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

}  // namespace

int main() {
  using namespace bootviz::scene;
  const auto& stages = BootStages();
  Check(stages.size() == 8, "eight stages");
  float total = 0;
  for (size_t i = 0; i < stages.size(); ++i) {
    const Stage& s = stages[i];
    const std::string id = "stage " + std::to_string(i + 1) + " (" + s.name + ")";
    Check(s.duration > 0, id + ": positive duration");
    Check(!s.steps.empty() && !s.title.empty() && !s.actor.empty() && !s.where.empty(),
          id + ": has title, actor, source and steps");
    Check(s.showcase >= 0 && s.showcase <= 1, id + ": showcase in [0,1]");
    Check(!s.log.empty() || !s.console_note.empty(), id + ": prints something or says why not");
    float prev = -1;
    for (const LogLine& l : s.log) {
      Check(l.at >= prev && l.at >= 0 && l.at <= 1, id + ": log times ascend within [0,1]");
      prev = l.at;
    }
    for (const Flow& f : s.flows) {
      Check(f.from != f.to && f.from < Part::kCount && f.to < Part::kCount, id + ": flow ends");
      Check(f.start >= 0 && f.start <= f.end && f.end <= 1, id + ": flow window");
      Check(f.label_at >= 0 && f.label_at <= 1, id + ": label position");
    }
    total += s.duration;
  }
  Check(std::fabs(total - TotalDuration()) < 1e-4f, "TotalDuration sums the stages");

  // Cursor <-> seconds round trip, and clamping at both ends.
  for (float t = 0; t < TotalDuration(); t += 0.37f) {
    const Cursor c = CursorAt(t);
    Check(std::fabs(SecondsAt(c) - t) < 1e-3f, "round trip at t=" + std::to_string(t));
  }
  Check(CursorAt(-5).stage == 0 && CursorAt(-5).progress == 0, "clamps before the start");
  const Cursor end = CursorAt(TotalDuration() + 10);
  Check(end.stage == static_cast<int>(stages.size()) - 1 && end.progress == 1, "clamps at the end");

  // The console only grows, and the firmware stages print nothing.
  size_t prev_lines = 0;
  for (float t = 0; t <= TotalDuration(); t += 0.25f) {
    const size_t n = ConsoleUpTo(CursorAt(t)).size();
    Check(n >= prev_lines, "console never shrinks");
    prev_lines = n;
  }
  Check(ConsoleUpTo({3, 1.f}).empty(), "nothing printed before start_kernel");
  const auto all = ConsoleUpTo({static_cast<int>(stages.size()) - 1, 1.f});
  Check(!all.empty() && all.front().find("OluxOS 0.3.0") != std::string::npos,
        "the first console line is the kernel banner");

  // Memory regions fit in the first GiB and appear in existing stages.
  for (const MemRegion& r : MemoryMap()) {
    Check(r.size > 0 && r.base + r.size <= (1ull << 30), r.label + ": inside 1 GiB");
    Check(r.stage >= 0 && r.stage < static_cast<int>(stages.size()), r.label + ": stage");
  }

  if (failures) return 1;
  std::printf("boot_script_test: all checks passed\n");
  return 0;
}
