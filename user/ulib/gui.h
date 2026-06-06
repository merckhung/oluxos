#ifndef _GUI_H_
#define _GUI_H_

#include <types.h>
#include "gles.h"

#define FB_WIDTH 640
#define FB_HEIGHT 480

void draw_rect_px(int x, int y, int w, int h, GLubyte r, GLubyte g, GLubyte b);
void draw_line_px(int x0, int y0, int x1, int y1, GLubyte r, GLubyte g,
                  GLubyte b);
void gui_draw_char(char c, int x, int y, int scale, GLubyte r, GLubyte g,
                   GLubyte b);
void gui_draw_string(const char *s, int x, int y, int scale, GLubyte r,
                     GLubyte g, GLubyte b);

typedef struct _Window {
  int id;
  int x, y;
  int w, h;
  const char* title;
  bool active;
  void (*draw_content)(struct _Window* win);
  struct _Window* prev;
  struct _Window* next;
} Window;

void win_init(Window* win, int id, int x, int y, int w, int h,
              const char* title, void (*draw_content)(Window*));
void win_draw(Window* win);

void draw_button(int x, int y, int w, int h, const char* label);
void draw_scrollbar(int x, int y, int w, int h, int slider_y, int slider_h);

#endif
