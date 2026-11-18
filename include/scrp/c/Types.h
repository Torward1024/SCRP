#ifndef SCRP_C_TYPES_H
#define SCRP_C_TYPES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* C17 boundary. Units: meters, kilograms, seconds, radians; right-handed Y-up.
   No exceptions cross the boundary. Errors are optional, caller-owned buffers.
   Handle owners are single-threaded unless an API explicitly says otherwise. */
typedef struct scrp_error { char message[512]; } scrp_error;
typedef struct scrp_vec3 { float x, y, z; } scrp_vec3;
typedef struct scrp_quat { float x, y, z, w; } scrp_quat;
typedef struct scrp_pose { scrp_vec3 position; scrp_quat rotation; } scrp_pose;
typedef struct scrp_color { float r, g, b, a; } scrp_color;
typedef struct scrp_vertex { scrp_vec3 position, normal; scrp_color color; } scrp_vertex;

scrp_vec3 scrp_vec3_add(scrp_vec3 a, scrp_vec3 b);
scrp_vec3 scrp_vec3_sub(scrp_vec3 a, scrp_vec3 b);
scrp_vec3 scrp_vec3_scale(scrp_vec3 a, float scale);
float scrp_vec3_dot(scrp_vec3 a, scrp_vec3 b);
scrp_vec3 scrp_vec3_cross(scrp_vec3 a, scrp_vec3 b);
scrp_vec3 scrp_vec3_normalize(scrp_vec3 a);
/* Rotations must be unit quaternions. A degenerate axis produces identity. */
scrp_quat scrp_quat_axis_angle(scrp_vec3 axis, float radians);
scrp_quat scrp_quat_mul(scrp_quat a, scrp_quat b);
scrp_vec3 scrp_quat_rotate(scrp_quat rotation, scrp_vec3 vector);
scrp_vec3 scrp_pose_point(scrp_pose pose, scrp_vec3 point);
/* Clamped linear position / shortest-path normalized quaternion interpolation. */
scrp_pose scrp_pose_lerp(scrp_pose from, scrp_pose to, float alpha);

#ifdef __cplusplus
}
#endif
#endif
