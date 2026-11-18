#include <scrp/c/Mesh.h>
#include "CApiInternal.h"
#include <algorithm>

extern "C" int scrp_mesh_deform(const scrp_vertex *src,const scrp_vec3 *delta,const float *weights,uint32_t count,float limit,scrp_vertex *out,scrp_bounds3 *bounds,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!src || !delta || !out || !bounds || !count || count%3 || count>3000000 || !std::isfinite(limit) || limit<0 || limit>10000)
            return scrp::capi::fail(error,"Deformation requires triangle buffers, bounds and a finite displacement limit");
        for (uint32_t i=0;i<count;++i) {
            const auto c=src[i].color;
            if (!scrp::capi::bounded(src[i].position,1e6f) || !scrp::capi::finite(src[i].normal) || !scrp::capi::bounded(delta[i],1e6f) ||
                !std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b) || !std::isfinite(c.a) ||
                (weights && (!std::isfinite(weights[i]) || weights[i]<0 || weights[i]>1)))
                return scrp::capi::fail(error,"Invalid deformation vertex, displacement or protection weight");
        }
        scrp_bounds3 b{{1e30f,1e30f,1e30f},{-1e30f,-1e30f,-1e30f}};
        for (uint32_t i=0;i<count;i+=3) {
            scrp_vertex face[3];
            for (int j=0;j<3;++j) {
                const auto index=i+j; face[j]=src[index];
                auto d=scrp_vec3_scale(delta[index],weights?weights[index]:1);
                const float length=std::sqrt(scrp_vec3_dot(d,d));
                if (length>limit) d=scrp_vec3_scale(d,limit/length);
                face[j].position=scrp_vec3_add(face[j].position,d);
            }
            const auto normal=scrp_vec3_normalize(scrp_vec3_cross(scrp_vec3_sub(face[1].position,face[0].position),scrp_vec3_sub(face[2].position,face[0].position)));
            for (int j=0;j<3;++j) {
                if (scrp_vec3_dot(normal,normal)>.5f) face[j].normal=normal;
                out[i+j]=face[j]; const auto p=face[j].position;
                b.minimum={std::min(b.minimum.x,p.x),std::min(b.minimum.y,p.y),std::min(b.minimum.z,p.z)};
                b.maximum={std::max(b.maximum.x,p.x),std::max(b.maximum.y,p.y),std::max(b.maximum.z,p.z)};
            }
        }
        *bounds=b; return 1;
    });
}
