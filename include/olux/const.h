#ifndef OLUX_CONST_H
#define OLUX_CONST_H

/* Constants usable from both C and assembly. */
#ifdef __ASSEMBLY__
#define _AC(X, Y) X
#else
#define __AC(X, Y) (X##Y)
#define _AC(X, Y) __AC(X, Y)
#endif
#define UL(x) _AC(x, UL)

#endif
