/* See mf_font.h. The UI font through C-Bricks' own font calls (cb_load_font / cb_draw_text_font): no raylib here any more.
   Rasterised at twice the size and drawn scaled down: smoother. */
#include "cbricks.h"
#include "mf_font.h"
static cb_font F;
static int loaded;
int mf_font_load(const char *path, int px) {
    if (loaded) return 1;
    F = cb_load_font(path, px * 2);
    loaded = F.id != 0;   /* id 0 = unreadable file: the caller falls back to cb_draw_text's font */
    return loaded;
}
int mf_font_ok(void) { return loaded; }
void mf_font_draw(const char *text, int x, int y, int px, int r, int g, int b, int a) {
    if (!loaded) return;
    cb_draw_text_font(F, text, x, y, px, (cb_rgba){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}
int mf_font_width(const char *text, int px) { return loaded ? cb_text_width_font(F, text, px) : 0; }
void mf_font_unload(void) { loaded = 0; }   /* C-Bricks frees fonts itself, before the window closes */
