#ifndef MF_SHED_H
#define MF_SHED_H
/* The node canvas: a .cbshader drawn as boxes and wires you can pan and zoom (pan, zoom, move nodes, type unwired values; every finished edit is saved to the file). */
void mf_shed_init(const char *argv0_dir);                /* loads the node table; returns quietly when it can't (the canvas says so) */
void mf_shed_load(const char *text);                     /* a new version of the file: keeps the view; the first load fits the graph */
void mf_shed_frame(int x, int y, int w, int h);          /* draw into the rectangle and handle the mouse there */
void mf_shed_set_path(const char *path);                 /* the file edits are saved to */
void mf_shed_type(const char *chars, int n);             /* the characters typed this frame (the app drains the keyboard once and hands them over) */
int mf_shed_editing(void);                               /* a value is being typed: the workspace's hotkeys must wait */
const char *mf_shed_take_open(void);                     /* a new file the canvas made ("" if none): the workspace switches to it */
int mf_shed_node_count(void);
int mf_shed_wire_count(void);
const char *mf_shed_note(void);                          /* "" or what is wrong (no node table, file is not a graph) */
#endif
