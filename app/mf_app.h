/* C-Vert app glue: the functions main.cbricks calls (as "Call C function" bricks). The brick graph
   owns the window and the frame; everything else lives in C, on top of the core (core/mf_*.h)
   and C-Bricks' own drawing calls (cbricks.h: cb_camera, cb_draw_mesh, cb_draw_line3d ...).
   Only mf_app.c includes cbricks.h: the core never sees a graphics library. */
#ifndef MF_APP_H
#define MF_APP_H

int mf_app_init(void);          /* after Open window: loads --obj FILE (else a cube), frames the camera; 0 ok, 1 failed */
void mf_app_update(void);       /* once per frame, first: input, camera, edits, demo script */
void mf_app_draw3d(void);       /* inside Draw 3D: grid, solid mesh, edges, selection */
void mf_app_draw_hud(void);     /* after Draw 3D (2D again): stats and key help */
void mf_app_shutdown(void);     /* in the Game loop's Done: frees the mesh and the undo history */

#endif
