#include <scrp/c/Types.h>
#include <algorithm>
#include <cmath>

extern "C" {
scrp_vec3 scrp_vec3_add(scrp_vec3 a, scrp_vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
scrp_vec3 scrp_vec3_sub(scrp_vec3 a, scrp_vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
scrp_vec3 scrp_vec3_scale(scrp_vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
float scrp_vec3_dot(scrp_vec3 a, scrp_vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
scrp_vec3 scrp_vec3_cross(scrp_vec3 a, scrp_vec3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
scrp_vec3 scrp_vec3_normalize(scrp_vec3 a) {
    const float length = std::sqrt(scrp_vec3_dot(a,a));
    return length > 1e-10f && std::isfinite(length) ? scrp_vec3_scale(a, 1.0f/length) : scrp_vec3{};
}
scrp_quat scrp_quat_axis_angle(scrp_vec3 axis, float radians) {
    axis = scrp_vec3_normalize(axis);
    if (scrp_vec3_dot(axis,axis) == 0 || !std::isfinite(radians)) return {0,0,0,1};
    const float s = std::sin(radians*0.5f);
    return {axis.x*s,axis.y*s,axis.z*s,std::cos(radians*0.5f)};
}
scrp_quat scrp_quat_mul(scrp_quat a, scrp_quat b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
scrp_vec3 scrp_quat_rotate(scrp_quat q, scrp_vec3 v) {
    const scrp_vec3 u{q.x,q.y,q.z};
    const auto t = scrp_vec3_scale(scrp_vec3_cross(u,v), 2.0f);
    return scrp_vec3_add(v, scrp_vec3_add(scrp_vec3_scale(t,q.w), scrp_vec3_cross(u,t)));
}
scrp_vec3 scrp_pose_point(scrp_pose p, scrp_vec3 v) { return scrp_vec3_add(p.position,scrp_quat_rotate(p.rotation,v)); }
scrp_pose scrp_pose_lerp(scrp_pose a, scrp_pose b, float alpha) {
    const float t = std::isfinite(alpha) ? std::clamp(alpha,0.0f,1.0f) : 0.0f;
    const float dot = a.rotation.x*b.rotation.x+a.rotation.y*b.rotation.y+a.rotation.z*b.rotation.z+a.rotation.w*b.rotation.w;
    const float s = dot < 0 ? -t : t;
    scrp_quat q{(1-t)*a.rotation.x+s*b.rotation.x, (1-t)*a.rotation.y+s*b.rotation.y,
                (1-t)*a.rotation.z+s*b.rotation.z, (1-t)*a.rotation.w+s*b.rotation.w};
    const float n = std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if (n > 1e-10f) q = {q.x/n,q.y/n,q.z/n,q.w/n}; else q = {0,0,0,1};
    return {scrp_vec3_add(scrp_vec3_scale(a.position,1-t),scrp_vec3_scale(b.position,t)), q};
}
}
