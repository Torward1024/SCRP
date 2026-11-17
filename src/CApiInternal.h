#pragma once
#include <scrp/c/Types.h>
#include <cmath>
#include <cstdio>
#include <exception>

namespace scrp { namespace capi {
inline int fail(scrp_error *error, const char *message) noexcept {
    if (error) std::snprintf(error->message, sizeof(error->message), "%s", message);
    return 0;
}
template<class F> int boundary(scrp_error *error, F operation) noexcept {
    if (error) error->message[0] = '\0';
    try { return operation(); }
    catch (const std::exception& e) { return fail(error, e.what()); }
    catch (...) { return fail(error, "Unexpected engine error"); }
}
inline bool finite(scrp_vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline bool bounded(scrp_vec3 v, float limit) { return finite(v) && std::abs(v.x)<=limit && std::abs(v.y)<=limit && std::abs(v.z)<=limit; }
inline bool valid(scrp_pose p) {
    const auto q = p.rotation;
    const double n = double(q.x)*q.x + double(q.y)*q.y + double(q.z)*q.z + double(q.w)*q.w;
    return bounded(p.position,1e6f) && std::isfinite(n) && std::abs(n - 1.0) < 0.002;
}
}}
