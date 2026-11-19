/* Window tricks through raylib directly (C-Bricks has no call for them yet: CBRICKS-UPDATE-NOTE requests). */
#ifndef MF_WIN_H
#define MF_WIN_H
void mf_window_borderless_toggle(void);   /* F11: ToggleBorderlessWindowed (the window manager ignores MaximizeWindow here) */
double mf_win_clock(void);                /* seconds on a fine clock (raylib GetTime; clock() without raylib): for --prof */
int mf_window_maximize(void);             /* MaximizeWindow; 1 once maximized (the caller asks again); no-op without raylib */
#endif
