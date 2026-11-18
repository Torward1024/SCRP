#ifndef SCRP_C_MODEL_H
#define SCRP_C_MODEL_H
#include "Types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct scrp_model scrp_model;
/* Local mesh-space triangles, expanded in draw order. UVs use a top-left origin.
   All pointers remain valid until the model is destroyed. Missing mesh names
   are empty strings, omitted colors are white, missing UVs yield NULL. */
typedef struct scrp_model_primitive {
    const char *mesh_name;
    uint32_t primitive_index, vertex_count;
    const scrp_vertex *vertices;
    const scrp_uv_vertex *uv_vertices;
    int32_t material_index; /* -1 for the default glTF material. */
    int32_t base_color_image_index; /* -1 if the material has no base color texture. */
} scrp_model_primitive;
/* Initialize *out to NULL. File paths are UTF-8. The memory variant can consume
   an existing VFS buffer, which is safe to release after the call. */
int scrp_model_load_glb(const char *path,scrp_model **out,scrp_error *error);
int scrp_model_decode_glb(const uint8_t *data,uint32_t size,scrp_model **out,scrp_error *error);
void scrp_model_destroy(scrp_model **model);
uint32_t scrp_model_primitive_count(const scrp_model *model);
const scrp_model_primitive *scrp_model_get_primitive(const scrp_model *model,uint32_t index);
/* Finds the first mesh with this exact name and mesh-local primitive index. */
const scrp_model_primitive *scrp_model_find_primitive(const scrp_model *model,const char *mesh_name,uint32_t primitive_index);
uint32_t scrp_model_image_count(const scrp_model *model);
/* Borrow the original encoded PNG/JPEG bytes from an embedded image buffer view.
   Optional outputs are cleared on failure. Decoding is the caller's choice. */
const uint8_t *scrp_model_image_data(const scrp_model *model,uint32_t image_index,size_t *byte_count,const char **mime_type);
#ifdef __cplusplus
}
#endif
#endif
