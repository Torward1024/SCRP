#ifndef SCRP_C_PHYSICS3D_H
#define SCRP_C_PHYSICS3D_H
#include "Types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct scrp_world scrp_world;
/* Opaque, process-unique IDs. Zero means none. Stale/cross-world IDs are rejected. */
typedef uint64_t scrp_body_id;
typedef struct scrp_world_desc { scrp_vec3 gravity; uint32_t max_bodies; } scrp_world_desc;
typedef struct scrp_body_desc {
    scrp_pose pose;
    scrp_vec3 half_extent;
    /* Zero mass creates a static box; otherwise inertia is derived from its shape. */
    float mass, friction, restitution, linear_damping, angular_damping;
    int continuous;
} scrp_body_desc;
typedef struct scrp_body_state {
    scrp_pose pose;
    scrp_vec3 linear_velocity, angular_velocity;
    int active;
} scrp_body_state;
typedef struct scrp_ray_hit {
    int hit;
    scrp_body_id body;
    float fraction;
    scrp_vec3 point, normal, velocity;
} scrp_ray_hit;

/* Initialize *out to NULL; failed creation leaves it unchanged. Destroy clears it.
   The world owns all its bodies. All operations on a world use one thread.
   Independent worlds can coexist; callers must not initialize Jolt separately. */
int scrp_world_create(const scrp_world_desc *desc, scrp_world **out, scrp_error *error);
void scrp_world_destroy(scrp_world **world);
int scrp_world_add_box(scrp_world *world, const scrp_body_desc *desc, scrp_body_id *out, scrp_error *error);
int scrp_world_remove_body(scrp_world *world, scrp_body_id body, scrp_error *error);
int scrp_world_body_state(const scrp_world *world, scrp_body_id body, scrp_body_state *out, scrp_error *error);
/* Teleport resets velocities and accumulated forces. Intended for spawn/reset. */
int scrp_world_set_pose(scrp_world *world, scrp_body_id body, scrp_pose pose, scrp_error *error);
int scrp_world_apply_force(scrp_world *world, scrp_body_id body, scrp_vec3 force, scrp_vec3 point, scrp_error *error);
int scrp_world_apply_impulse(scrp_world *world, scrp_body_id body, scrp_vec3 impulse, scrp_vec3 point, scrp_error *error);
/* One explicit fixed step, 0 < dt <= 1/30 second. Forces are consumed by this step. */
int scrp_world_step(scrp_world *world, float dt, scrp_error *error);
/* Closest surface hit along a displacement (not a unit direction). Ignore 0 or one
   valid body. No hit is success with hit=0. Hits include surface point velocity. */
int scrp_world_raycast(const scrp_world *world, scrp_vec3 origin, scrp_vec3 displacement,
                       scrp_body_id ignore, scrp_ray_hit *out, scrp_error *error);
const char *scrp_physics_backend(void);
#ifdef __cplusplus
}
#endif
#endif
