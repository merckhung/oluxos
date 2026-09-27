#version 450
// Animated printed-circuit-board backdrop. A dark board with a fine grid
// of traces; bright pulses travel along the traces and the whole board
// glows faintly in the current boot stage's accent colour. Everything the
// Skia layer leaves transparent shows this.

layout(push_constant) uniform Params {
  float time;      // seconds
  float progress;  // 0..1 through the boot
  vec2 size;       // framebuffer size in pixels
  vec4 accent;     // rgb, intensity
} p;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

float hash(vec2 v) { return fract(sin(dot(v, vec2(127.1, 311.7))) * 43758.5453); }

// Distance to the nearest trace of a grid with the given pitch, where only
// some rows/columns carry a trace (chosen per cell with a hash).
float traces(vec2 px, float pitch, float seed, out vec2 cell, out float horizontal) {
  vec2 g = px / pitch;
  cell = floor(g);
  vec2 f = fract(g) - 0.5;
  float row = step(0.55, hash(vec2(cell.y, seed)));
  float col = step(0.55, hash(vec2(cell.x, seed + 7.0)));
  float dh = row > 0.5 ? abs(f.y) * pitch : 1e3;
  float dv = col > 0.5 ? abs(f.x) * pitch : 1e3;
  horizontal = dh < dv ? 1.0 : 0.0;
  return min(dh, dv);
}

void main() {
  vec2 px = v_uv * p.size;
  float scale = p.size.y / 900.0;

  // Board: deep blue-green with a soft vignette.
  vec3 board = mix(vec3(0.012, 0.030, 0.045), vec3(0.020, 0.055, 0.070), v_uv.y);
  float vig = smoothstep(1.25, 0.25, length(v_uv - 0.5) * 1.6);
  vec3 color = board * (0.55 + 0.45 * vig);

  // Fine dot grid.
  vec2 dots = abs(fract(px / (16.0 * scale)) - 0.5);
  color += vec3(0.03, 0.06, 0.07) * smoothstep(0.08, 0.0, max(dots.x, dots.y) - 0.02) * 0.35;

  // Traces with travelling pulses.
  vec2 cell;
  float horizontal;
  float d = traces(px, 48.0 * scale, 3.0, cell, horizontal);
  float line = smoothstep(1.4 * scale, 0.2 * scale, d);
  float along = horizontal > 0.5 ? px.x : px.y;
  float phase = hash(cell + horizontal * 13.0);
  float speed = (140.0 + 120.0 * phase) * scale;
  float pulse = fract((along - p.time * speed) / (600.0 * scale) + phase);
  pulse = smoothstep(0.0, 0.04, pulse) * smoothstep(0.10, 0.04, pulse);
  vec3 trace = vec3(0.05, 0.13, 0.15);
  color += line * (trace + p.accent.rgb * pulse * 0.9 * p.accent.a);

  // Stage glow from the top-left, growing as the boot progresses.
  float glow = exp(-length(v_uv - vec2(0.18, 0.1)) * 2.6);
  color += p.accent.rgb * glow * (0.05 + 0.07 * p.progress) * p.accent.a;

  out_color = vec4(color, 1.0);
}
