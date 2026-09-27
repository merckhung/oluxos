// The OluxOS boot on a Raspberry Pi 4B, as a sequence of stages: who runs,
// what happens, which parts of the board are involved, what moves between
// them, what lands in RAM and what the serial console prints.
//
// The stage content follows OluxOS's own sources (arch/arm64/head.S,
// kernel/main.c, kernel/init.c, user/prog/init, scripts/mksdcard.sh) and the
// Raspberry Pi 4 boot flow. Kernel and init console lines are recorded from
// OluxOS 0.3.0 booting the `make sdcard` image on QEMU's raspi4b machine,
// with a test-only option (rpi_thermal.trip) removed from the command line
// and its effect; noisy lines about QEMU-virt-only disks are left out. The
// closing shell session is illustrative. The firmware stages print nothing
// there (a real Pi prints them with uart_2ndstage=1).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bootviz::scene {

// Blocks of the board diagram.
enum class Part {
  kEeprom,
  kSdAuto,  // SD partition 1: autoboot.txt
  kSdBootA, // partition 2: boot slot A
  kSdBootB, // partition 3: boot slot B
  kSdData,  // partition 4: /data
  kRom,     // VPU boot ROM
  kVpu,     // VideoCore VI
  kMailbox,
  kCpu0,
  kCpu1,
  kCpu2,
  kCpu3,
  kGic,
  kTimer,
  kUart,
  kEmmc2,
  kGpio,
  kPm,  // power management / watchdog
  kPcie,
  kGenet,
  kRam,
  kVl805,
  kPhy,
  kSerial,  // the host's serial terminal (USB-serial on GPIO 14/15)
  kCount
};

enum class CpuState { kReset, kSpin, kRun, kIdle };

// Something moving between two parts during [start, end] of a stage
// (fractions of the stage).
struct Flow {
  Part from, to;
  std::string label;
  float start = 0.f, end = 1.f;
  uint32_t color = 0xFF7FD8FF;  // ARGB
  float label_at = 0.5f;        // where the label sits along the path
  int label_side = 1;           // on vertical runs: +1 right of the trace, -1 left
};

// A physical memory region that exists from `stage` on.
struct MemRegion {
  uint64_t base, size;
  std::string label;
  uint32_t color;
  int stage;
  float at = 0.f;  // appears at this fraction of `stage`
};

struct LogLine {
  float at;  // fraction of the stage
  std::string text;
};

struct Stage {
  std::string name;   // short, for the stage rail
  std::string title;  // headline
  std::string actor;  // who executes this stage
  std::string where;  // source reference
  std::vector<std::string> steps;
  float duration;     // seconds at 1x speed
  uint32_t accent;    // ARGB
  std::vector<Part> active;
  std::vector<Flow> flows;
  CpuState cpus[4];   // state of each ARM core during the stage
  int sd_slot = -1;   // highlighted boot slot partition (2 or 3), -1 = none
  std::vector<LogLine> log;
  std::string console_note;  // shown when the stage prints nothing
  float showcase = 0.75f;    // a representative moment, for screenshots
};

const std::vector<Stage>& BootStages();
const std::vector<MemRegion>& MemoryMap();

// Total length of the sequence at 1x speed.
float TotalDuration();

// Position in the sequence: stage index and progress within it (0..1).
struct Cursor {
  int stage = 0;
  float progress = 0.f;
};
Cursor CursorAt(float seconds);
float SecondsAt(Cursor c);

// Console lines printed up to `c`, oldest first.
std::vector<std::string> ConsoleUpTo(Cursor c);

}  // namespace bootviz::scene
