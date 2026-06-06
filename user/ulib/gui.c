#include "gui.h"
#include <string.h>
#include <types.h>
#include "gles.h"
#include "gui_font.h"

static GLfixed pixel_to_ndc_x(int px, int width) {
  return (GLfixed)((((int64_t)(px * 2 - width)) << 16) / width);
}

static GLfixed pixel_to_ndc_y(int py, int height) {
  return (GLfixed)((((int64_t)(height - py * 2)) << 16) / height);
}

void draw_rect_px(int x, int y, int w, int h, GLubyte r, GLubyte g, GLubyte b) {
  GLfixed x0 = pixel_to_ndc_x(x, FB_WIDTH);
  GLfixed y0 = pixel_to_ndc_y(y, FB_HEIGHT);
  GLfixed x1 = pixel_to_ndc_x(x + w, FB_WIDTH);
  GLfixed y1 = pixel_to_ndc_y(y + h, FB_HEIGHT);

  GLfixed vertices[] = {x0, y0, x1, y0, x0, y1, x1, y1};
  glColor4ub(r, g, b, 255);
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2, GL_FIXED, 0, vertices);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableClientState(GL_VERTEX_ARRAY);
}

void draw_line_px(int x0, int y0, int x1, int y1, GLubyte r, GLubyte g,
                  GLubyte b) {
  GLfixed fx0 = pixel_to_ndc_x(x0, FB_WIDTH);
  GLfixed fy0 = pixel_to_ndc_y(y0, FB_HEIGHT);
  GLfixed fx1 = pixel_to_ndc_x(x1, FB_WIDTH);
  GLfixed fy1 = pixel_to_ndc_y(y1, FB_HEIGHT);

  GLfixed vertices[] = {fx0, fy0, fx1, fy1};
  glColor4ub(r, g, b, 255);
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2, GL_FIXED, 0, vertices);
  glDrawArrays(GL_LINES, 0, 2);
  glDisableClientState(GL_VERTEX_ARRAY);
}

void gui_draw_char(char c, int x, int y, int scale, GLubyte r, GLubyte g,
                   GLubyte b) {
  const uint8_t* bitmap = get_gui_font_bitmap(c);
  int row, col;
  for (row = 0; row < 8; row++) {
    uint8_t row_byte = bitmap[row];
    for (col = 0; col < 8; col++) {
      if (row_byte & (1 << (7 - col))) {
        draw_rect_px(x + col * scale, y + row * scale, scale, scale, r, g, b);
      }
    }
  }
}

void gui_draw_string(const char *s, int x, int y, int scale, GLubyte r,
                     GLubyte g, GLubyte b) {
  int i = 0;
  while (s[i]) {
    gui_draw_char(s[i], x + i * 8 * scale, y, scale, r, g, b);
    i++;
  }
}

void win_init(Window* win, int id, int x, int y, int w, int h,
              const char* title, void (*draw_content)(Window*)) {
  win->id = id;
  win->x = x;
  win->y = y;
  win->w = w;
  win->h = h;
  win->title = title;
  win->active = false;
  win->draw_content = draw_content;
  win->prev = NULL;
  win->next = NULL;
}

void win_draw(Window* win) {
  // Border (black)
  draw_rect_px(win->x, win->y, win->w, win->h, 0, 0, 0);
  // Content background (grey)
  draw_rect_px(win->x + 1, win->y + 1, win->w - 2, win->h - 2, 200, 200, 200);

  // Title bar
  if (win->active) {
    draw_rect_px(win->x + 1, win->y + 1, win->w - 2, 20, 0, 0, 200);  // Blue
  } else {
    draw_rect_px(win->x + 1, win->y + 1, win->w - 2, 20, 100, 100,
                 100);  // Dark Grey
  }

  // Title text
  gui_draw_string(win->title, win->x + 5, win->y + 4, 1, 255, 255, 255);

  // Close button
  draw_rect_px(win->x + win->w - 16, win->y + 4, 12, 12, 250, 50, 50);
  gui_draw_string("x", win->x + win->w - 13, win->y + 6, 1, 255, 255, 255);

  // Content
  if (win->draw_content) {
    win->draw_content(win);
  }
}

void draw_button(int x, int y, int w, int h, const char* label) {
  draw_rect_px(x, y, w, h, 50, 50, 50);
  draw_rect_px(x + 1, y + 1, w - 2, h - 2, 220, 220, 220);

  int text_w = strlen(label) * 8;
  gui_draw_string(label, x + (w - text_w) / 2, y + (h - 8) / 2, 1, 0, 0, 0);
}

void draw_scrollbar(int x, int y, int w, int h, int slider_y, int slider_h) {
  draw_rect_px(x, y, w, h, 150, 150, 150);
  draw_rect_px(x + 1, y + 1, w - 2, h - 2, 180, 180, 180);
  draw_rect_px(x + 1, y + slider_y, w - 2, slider_h, 240, 240, 240);
}
