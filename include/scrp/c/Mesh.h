#ifndef SCRP_C_MESH_H
#define SCRP_C_MESH_H
#include "Types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct scrp_bounds3 { scrp_vec3 minimum, maximum; } scrp_bounds3;
/* General bounded displacement field, with optional per-vertex protection weights
   in 0..1. Rebuilds flat triangle normals and bounds. Colors are preserved.
   No allocation, graphics context or persistent mutation of source data. Output
   may equal source; otherwise buffers must not overlap. Displacements and weights
   must not overlap output storage. Invalid input leaves
   output and bounds unchanged. Restore elastic geometry by using pristine source. */
int scrp_mesh_deform(const scrp_vertex *source, const scrp_vec3 *displacements,
                     const float *weights, uint32_t count, float max_displacement,
                     scrp_vertex *out, scrp_bounds3 *bounds, scrp_error *error);
#ifdef __cplusplus
}
#endif
#endif
