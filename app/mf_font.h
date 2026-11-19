/* C-Vert UI font: a smooth TrueType font through C-Bricks' cb_load_font / cb_draw_text_font. Falls back to
   cb_draw_text's built-in font when the file did not load: mf_font_ok() says which. See DECISIONS.md. */
#ifndef MF_FONT_H
#define MF_FONT_H
int mf_font_load(const char *path, int px);               /* once, after the window is open; 1 ok */
int mf_font_ok(void);
void mf_font_draw(const char *text, int x, int y, int px, int r, int g, int b, int a);
int mf_font_width(const char *text, int px);              /* pixels; 0 when not loaded */
void mf_font_unload(void);
#endif
