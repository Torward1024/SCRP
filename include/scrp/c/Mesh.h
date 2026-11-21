#ifndef SCRP_C_MESH_H
#define SCRP_C_MESH_H
#include "Types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct scrp_bounds3 { scrp_vec3 minimum, maximum; } scrp_bounds3;
typedef struct scrp_dent { scrp_vec3 center, radii, displacement; } scrp_dent;
/* General bounded displacement field, with optional per-vertex protection weights
   in 0..1. Rebuilds flat triangle normals and bounds. Colors are preserved.
   No allocation, graphics context or persistent mutation of source data. Output
   may equal source; otherwise buffers must not overlap. Displacements and weights
   must not overlap output storage. Invalid input leaves
   output and bounds unchanged. Restore elastic geometry by using pristine source. */
int scrp_mesh_deform(const scrp_vertex *source, const scrp_vec3 *displacements,
                     const float *weights, uint32_t count, float max_displacement,
                     scrp_vertex *out, scrp_bounds3 *bounds, scrp_error *error);
/* Sample a caller-owned collection of persistent ellipsoidal dents on pristine
   textured triangles. The falloff is (1 - squared normalized distance)^2 inside
   each ellipsoid. Optional vertex weights in [0,1] scale the combined displacement
   before the length cap. Identical positions and weights yield identical positions
   across UV/material seams. Authored normals follow the field's local derivative.
   With no dents this restores the source verbatim. Source and output may coincide.
   Invalid input leaves output and bounds unchanged. At most 256 dents per call. */
int scrp_mesh_dent_uv(const scrp_uv_vertex *source, const float *weights, uint32_t count,
                      const scrp_dent *dents, uint32_t dent_count, float max_displacement,
                      scrp_uv_vertex *out, scrp_bounds3 *bounds, scrp_error *error);
#ifdef __cplusplus
}
#endif
#endif
