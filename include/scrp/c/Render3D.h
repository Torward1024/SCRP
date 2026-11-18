#ifndef SCRP_C_RENDER3D_H
#define SCRP_C_RENDER3D_H
#include "Types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct scrp_view3d scrp_view3d;
typedef uint64_t scrp_mesh_id;
typedef struct scrp_view_desc { const char *title; int width, height, hidden, vsync; } scrp_view_desc;
typedef struct scrp_view_input { int quit, focused, width, height; double elapsed_seconds; } scrp_view_input;
typedef struct scrp_spotlight { scrp_vec3 position, direction; scrp_color color; float range, cone_cos; } scrp_spotlight;
typedef struct scrp_camera {
    scrp_vec3 eye, target, up;
    float vertical_fov, near_plane, far_plane, fog_start, fog_end;
    scrp_color fog_color;
    scrp_spotlight lights[2]; /* range=0 disables a light. */
} scrp_camera;

/* Optional OpenGL 3.3 module; does not use SDL_Renderer. Use on the main thread.
   Initialize *out to NULL. The view owns its meshes; destroy clears the pointer. */
int scrp_view3d_create(const scrp_view_desc *desc, scrp_view3d **out, scrp_error *error);
void scrp_view3d_destroy(scrp_view3d **view);
int scrp_view3d_poll(scrp_view3d *view, scrp_view_input *out, scrp_error *error);
/* SDL physical key names (e.g. "A", "Space"); bindings belong to the consumer. */
int scrp_view3d_key_down(const scrp_view3d *view, const char *name);
/* Copies triangle vertices into GPU storage; caller retains the source memory. */
int scrp_view3d_mesh_create(scrp_view3d *view, const scrp_vertex *triangles, uint32_t count, scrp_mesh_id *out, scrp_error *error);
int scrp_view3d_mesh_destroy(scrp_view3d *view, scrp_mesh_id mesh, scrp_error *error);
/* Replace vertices of an owned mesh. Validate all input before touching GPU data. */
int scrp_view3d_mesh_update(scrp_view3d *view, scrp_mesh_id mesh,
                            const scrp_vertex *triangles, uint32_t count, scrp_error *error);
/* Centered unit box; cylinder is length 1 along X, radius 1 in YZ. Flat normals. */
int scrp_view3d_box_mesh(scrp_view3d *view, scrp_mesh_id *out, scrp_error *error);
int scrp_view3d_cylinder_mesh(scrp_view3d *view, uint32_t segments, scrp_mesh_id *out, scrp_error *error);
int scrp_view3d_begin(scrp_view3d *view, const scrp_camera *camera, scrp_error *error);
int scrp_view3d_draw(scrp_view3d *view, scrp_mesh_id mesh, scrp_pose pose, scrp_vec3 scale,
                     scrp_color tint, float emissive, scrp_error *error);
/* Overlay coordinates use a caller-selected logical canvas, top-left origin.
   Scene draws precede overlays; overlay calls disable depth testing. */
int scrp_view3d_overlay(scrp_view3d *view, const scrp_vertex *triangles, uint32_t count,
                       float canvas_width, float canvas_height, scrp_error *error);
int scrp_view3d_text(scrp_view3d *view, float x, float y, const char *text, float pixel_size,
                    scrp_color color, float canvas_width, float canvas_height, scrp_error *error);
/* Optional BMP capture reads the rendered back buffer before swapping. */
int scrp_view3d_present(scrp_view3d *view, const char *capture_path, scrp_error *error);
const char *scrp_view3d_driver(const scrp_view3d *view);
#ifdef __cplusplus
}
#endif
#endif
