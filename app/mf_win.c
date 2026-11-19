/* See mf_win.h. The ONLY file that includes raylib.h (not cbricks.h: the two define the same types): window tricks
   C-Bricks has no call for yet (maximize, borderless). Compiles to stubs where raylib.h is not there (MS-DOS backend). */
#include "mf_win.h"
#include <time.h>
#if defined(__has_include)
#  if __has_include("raylib.h")
#    define MF_HAVE_RAYLIB 1
#  endif
#endif
#ifdef MF_HAVE_RAYLIB
#include "raylib.h"
void mf_window_borderless_toggle(void) { ToggleBorderlessWindowed(); }
double mf_win_clock(void) { return GetTime(); }
int mf_window_maximize(void) { if (!IsWindowMaximized()) MaximizeWindow(); return IsWindowMaximized(); }
#else
void mf_window_borderless_toggle(void) {}
double mf_win_clock(void) { return (double)clock() / CLOCKS_PER_SEC; }
int mf_window_maximize(void) { return 1; }
#endif
