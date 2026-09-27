#ifndef _GLES_H_
#define _GLES_H_

#include <types.h>

#define GL_COLOR_BUFFER_BIT 0x00004000

#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FIXED 0x140C

#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076

#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006

typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLenum;
typedef int GLfixed;
typedef unsigned char GLubyte;
typedef unsigned int GLbitfield;
typedef void GLvoid;

#define INT_TO_FIXED(i) ((GLfixed)((i) << 16))
#define FIXED_TO_INT(f) ((int)((f) >> 16))
#define GL_ONE 65536
extern bool gl_debug;

void glInit(void* fb_addr, int width, int height);

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glClearColorx(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha);
void glClear(GLbitfield mask);
void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);

void glEnableClientState(GLenum array);
void glDisableClientState(GLenum array);
void glVertexPointer(GLint size, GLenum type, GLsizei stride,
                     const GLvoid* pointer);
void glColorPointer(GLint size, GLenum type, GLsizei stride,
                    const GLvoid* pointer);

void glDrawArrays(GLenum mode, GLint first, GLsizei count);

#endif
