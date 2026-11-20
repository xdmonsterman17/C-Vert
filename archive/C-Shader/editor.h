#ifndef EDITOR_H
#define EDITOR_H
/* The node canvas: a .cbshader drawn as boxes and wires you can pan and zoom (phase A: read-only). */
void editor_init(const char *argv0_dir);                /* loads the node table; returns quietly when it can't (the canvas says so) */
void editor_load(const char *text);                     /* a new version of the file: keeps the view; the first load fits the graph */
void editor_frame(int x, int y, int w, int h);          /* draw into the rectangle and handle the mouse there */
int editor_node_count(void);
int editor_wire_count(void);
const char *editor_note(void);                          /* "" or what is wrong (no node table, file is not a graph) */
#endif
