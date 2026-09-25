#include "gles.h"
#include <types.h>
#include "stdio.h"

bool gl_debug = false;

static void* gl_fb_addr = NULL;
static int gl_fb_width = 0;
static int gl_fb_height = 0;

static GLint gl_viewport_x = 0;
static GLint gl_viewport_y = 0;
static GLsizei gl_viewport_w = 0;
static GLsizei gl_viewport_h = 0;

static uint8_t gl_clear_r = 0;
static uint8_t gl_clear_g = 0;
static uint8_t gl_clear_b = 0;

static uint8_t gl_current_r = 255;
static uint8_t gl_current_g = 255;
static uint8_t gl_current_b = 255;

static bool gl_vertex_array_enabled = false;
static bool gl_color_array_enabled = false;

static const GLfixed* gl_vertex_pointer = NULL;
static GLint gl_vertex_size = 2;
static GLsizei gl_vertex_stride = 0;

static const GLubyte* gl_color_pointer = NULL;
static GLint gl_color_size = 4;
static GLsizei gl_color_stride = 0;

void glInit(void* fb_addr, int width, int height) {
  gl_fb_addr = fb_addr;
  gl_fb_width = width;
  gl_fb_height = height;

  gl_viewport_x = 0;
  gl_viewport_y = 0;
  gl_viewport_w = width;
  gl_viewport_h = height;
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
  gl_viewport_x = x;
  gl_viewport_y = y;
  gl_viewport_w = width;
  gl_viewport_h = height;
}

void glClearColorx(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha) {
  gl_clear_r = (uint8_t)((red * 255) >> 16);
  gl_clear_g = (uint8_t)((green * 255) >> 16);
  gl_clear_b = (uint8_t)((blue * 255) >> 16);
}

void glClear(GLbitfield mask) {
  if (mask & GL_COLOR_BUFFER_BIT) {
    if (!gl_fb_addr) return;
    uint8_t* fb = gl_fb_addr;
    int i;
    for (i = 0; i < gl_fb_width * gl_fb_height; i++) {
      fb[i * 3] = gl_clear_r;
      fb[i * 3 + 1] = gl_clear_g;
      fb[i * 3 + 2] = gl_clear_b;
    }
  }
}

void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha) {
  gl_current_r = red;
  gl_current_g = green;
  gl_current_b = blue;
}

void glEnableClientState(GLenum array) {
  if (array == GL_VERTEX_ARRAY) {
    gl_vertex_array_enabled = true;
  } else if (array == GL_COLOR_ARRAY) {
    gl_color_array_enabled = true;
  }
}

void glDisableClientState(GLenum array) {
  if (array == GL_VERTEX_ARRAY) {
    gl_vertex_array_enabled = false;
  } else if (array == GL_COLOR_ARRAY) {
    gl_color_array_enabled = false;
  }
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride,
                     const GLvoid* pointer) {
  gl_vertex_size = size;
  gl_vertex_stride = stride;
  gl_vertex_pointer = (const GLfixed*)pointer;
}

void glColorPointer(GLint size, GLenum type, GLsizei stride,
                    const GLvoid* pointer) {
  gl_color_size = size;
  gl_color_stride = stride;
  gl_color_pointer = (const GLubyte*)pointer;
}

static void draw_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if (x >= 0 && x < gl_fb_width && y >= 0 && y < gl_fb_height) {
    uint8_t* fb = gl_fb_addr;
    int offset = (y * gl_fb_width + x) * 3;
    fb[offset] = r;
    fb[offset + 1] = g;
    fb[offset + 2] = b;
  }
}

static void draw_line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g,
                      uint8_t b) {
  int dx = x1 - x0;
  if (dx < 0) dx = -dx;
  int dy = y1 - y0;
  if (dy < 0) dy = -dy;
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;

  while (1) {
    draw_pixel(x0, y0, r, g, b);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static int edge_function(int ax, int ay, int bx, int by, int cx, int cy) {
  return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

static void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                          uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1,
                          uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2,
                          uint8_t b2, bool interpolate_color) {
  int min_x = x0;
  if (x1 < min_x) min_x = x1;
  if (x2 < min_x) min_x = x2;

  int max_x = x0;
  if (x1 > max_x) max_x = x1;
  if (x2 > max_x) max_x = x2;

  int min_y = y0;
  if (y1 < min_y) min_y = y1;
  if (y2 < min_y) min_y = y2;

  int max_y = y0;
  if (y1 > max_y) max_y = y1;
  if (y2 > max_y) max_y = y2;

  if (min_x < 0) min_x = 0;
  if (max_x >= gl_fb_width) max_x = gl_fb_width - 1;
  if (min_y < 0) min_y = 0;
  if (max_y >= gl_fb_height) max_y = gl_fb_height - 1;

  int area = edge_function(x0, y0, x1, y1, x2, y2);
  if (area == 0) return;
  int abs_area = area < 0 ? -area : area;
  int pixels_drawn = 0;

  int x, y;
  for (y = min_y; y <= max_y; y++) {
    for (x = min_x; x <= max_x; x++) {
      int w0 = edge_function(x1, y1, x2, y2, x, y);
      int w1 = edge_function(x2, y2, x0, y0, x, y);
      int w2 = edge_function(x0, y0, x1, y1, x, y);

      bool inside = false;
      if (area > 0) {
        if (w0 >= 0 && w1 >= 0 && w2 >= 0) inside = true;
      } else {
        if (w0 <= 0 && w1 <= 0 && w2 <= 0) inside = true;
      }

      if (inside) {
        pixels_drawn++;
        if (interpolate_color) {
          int abs_w0 = w0 < 0 ? -w0 : w0;
          int abs_w1 = w1 < 0 ? -w1 : w1;
          int abs_w2 = w2 < 0 ? -w2 : w2;

          uint8_t r =
              (uint8_t)((abs_w0 * r0 + abs_w1 * r1 + abs_w2 * r2) / abs_area);
          uint8_t g =
              (uint8_t)((abs_w0 * g0 + abs_w1 * g1 + abs_w2 * g2) / abs_area);
          uint8_t b =
              (uint8_t)((abs_w0 * b0 + abs_w1 * b1 + abs_w2 * b2) / abs_area);
          draw_pixel(x, y, r, g, b);
        } else {
          draw_pixel(x, y, r0, g0, b0);
        }
      }
    }
  }

}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
  if (!gl_vertex_array_enabled || !gl_vertex_pointer) return;

  int v_stride = gl_vertex_stride ? (gl_vertex_stride / sizeof(GLfixed))
                                  : gl_vertex_size;
  int c_stride =
      gl_color_stride ? (gl_color_stride / sizeof(GLubyte)) : gl_color_size;

#define GET_VERTEX(idx, out_x, out_y)                                         \
  {                                                                           \
    const GLfixed* v = gl_vertex_pointer + (first + idx) * v_stride;          \
    GLfixed nx = v[0];                                                        \
    GLfixed ny = v[1];                                                        \
    out_x = gl_viewport_x + (((nx + GL_ONE) * gl_viewport_w) >> 17);          \
    out_y = gl_viewport_y + (((GL_ONE - ny) * gl_viewport_h) >> 17);          \
  }

#define GET_COLOR(idx, out_r, out_g, out_b)                                   \
  {                                                                           \
    if (gl_color_array_enabled && gl_color_pointer) {                         \
      const GLubyte* c = gl_color_pointer + (first + idx) * c_stride;         \
      out_r = c[0];                                                           \
      out_g = c[1];                                                           \
      out_b = c[2];                                                           \
    } else {                                                                  \
      out_r = gl_current_r;                                                   \
      out_g = gl_current_g;                                                   \
      out_b = gl_current_b;                                                   \
      (void)c_stride;                                                         \
    }                                                                         \
  }

  if (mode == GL_POINTS) {
    int i;
    for (i = 0; i < count; i++) {
      int sx, sy;
      uint8_t r, g, b;
      GET_VERTEX(i, sx, sy);
      GET_COLOR(i, r, g, b);
      draw_pixel(sx, sy, r, g, b);
    }
  } else if (mode == GL_LINES) {
    int i;
    for (i = 0; i < count - 1; i += 2) {
      int x0, y0, x1, y1;
      uint8_t r0, g0, b0;
      GET_VERTEX(i, x0, y0);
      GET_COLOR(i, r0, g0, b0);
      GET_VERTEX(i + 1, x1, y1);
      draw_line(x0, y0, x1, y1, r0, g0, b0);
    }
  } else if (mode == GL_LINE_STRIP) {
    int i;
    for (i = 0; i < count - 1; i++) {
      int x0, y0, x1, y1;
      uint8_t r0, g0, b0;
      GET_VERTEX(i, x0, y0);
      GET_COLOR(i, r0, g0, b0);
      GET_VERTEX(i + 1, x1, y1);
      draw_line(x0, y0, x1, y1, r0, g0, b0);
    }
  } else if (mode == GL_TRIANGLES) {
    int i;
    for (i = 0; i < count - 2; i += 3) {
      int x0, y0, x1, y1, x2, y2;
      uint8_t r0, g0, b0, r1, g1, b1, r2, g2, b2;
      GET_VERTEX(i, x0, y0);
      GET_COLOR(i, r0, g0, b0);
      GET_VERTEX(i + 1, x1, y1);
      GET_COLOR(i + 1, r1, g1, b1);
      GET_VERTEX(i + 2, x2, y2);
      GET_COLOR(i + 2, r2, g2, b2);
      draw_triangle(x0, y0, x1, y1, x2, y2, r0, g0, b0, r1, g1, b1, r2, g2, b2,
                    gl_color_array_enabled);
    }
  } else if (mode == GL_TRIANGLE_STRIP) {
    int i;
    for (i = 0; i < count - 2; i++) {
      int x0, y0, x1, y1, x2, y2;
      uint8_t r0, g0, b0, r1, g1, b1, r2, g2, b2;
      if (i % 2 == 0) {
        GET_VERTEX(i, x0, y0);
        GET_COLOR(i, r0, g0, b0);
        GET_VERTEX(i + 1, x1, y1);
        GET_COLOR(i + 1, r1, g1, b1);
      } else {
        GET_VERTEX(i + 1, x0, y0);
        GET_COLOR(i + 1, r0, g0, b0);
        GET_VERTEX(i, x1, y1);
        GET_COLOR(i, r1, g1, b1);
      }
      GET_VERTEX(i + 2, x2, y2);
      GET_COLOR(i + 2, r2, g2, b2);
      draw_triangle(x0, y0, x1, y1, x2, y2, r0, g0, b0, r1, g1, b1, r2, g2, b2,
                    gl_color_array_enabled);
    }
  }
}
