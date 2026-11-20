#ifndef MF_SHADER_H
#define MF_SHADER_H
/* The Shader workspace (F4): a .cbshader beside a live preview. The preview polls the file four times a second and reloads it
   (the C-Bricks runtime's own GLSL emitter, on the real GPU); the node canvas (mf_shed.c) shows the graph. Part of C-Vert since
   2026-11-19 (it began as the C-Shader program). */
int mf_shader_active(void);
void mf_shader_open(const char *path);      /* "" = the last one / none; switches the workspace on */
void mf_shader_close(void);
int mf_shader_split(void);                  /* y where the graph editor starts (the preview is above it) */
int mf_shader_model(void);                  /* 1: the preview shows the scene's model, 0: the sphere */
void mf_shader_set_model(int on);
void mf_shader_typed(const char *chars, int n);   /* characters typed this frame (for a value being edited on the canvas) */
void mf_shader_update(void);                /* once a frame while active: polling, the sphere's spin */
void mf_shader_draw3d(float cx, float cy, float cz, float size, void *model, int model_tris);   /* the preview, centred there */
void mf_shader_draw_hud(void);              /* the canvas + the status line (2D) */
float mf_shader_shift(void);                /* how far (in half-heights) the camera should slide so the preview sits in the top strip: see place_camera */
void mf_shader_shutdown(void);
const char *mf_shader_status(void);
#endif
