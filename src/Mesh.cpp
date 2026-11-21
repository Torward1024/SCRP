#include <scrp/c/Mesh.h>
#include "CApiInternal.h"
#include <algorithm>
#include <array>

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

namespace scrp { namespace {
using triple=std::array<double,3>;
triple coordinates(scrp_vec3 v) { return {v.x,v.y,v.z}; }
triple cross(const triple& a,const triple& b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
double dot(const triple& a,const triple& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

/* Extend barycentrically interpolated weights off each triangle with zero
   derivative perpendicular to it. Uniform and degenerate faces need no slope. */
triple weight_gradient(const scrp_uv_vertex *face,const float *weight) {
    if (!weight || (weight[0]==weight[1] && weight[0]==weight[2])) return {};
    const auto a=coordinates(scrp_vec3_sub(face[1].vertex.position,face[0].vertex.position));
    const auto b=coordinates(scrp_vec3_sub(face[2].vertex.position,face[0].vertex.position));
    const auto norm=cross(a,b);
    const double area2=dot(norm,norm);
    if (area2<1e-28) return {};
    const auto du=cross(b,norm),dv=cross(norm,a);
    triple result;
    for (int i=0;i<3;++i) result[i]=((weight[1]-weight[0])*du[i]+(weight[2]-weight[0])*dv[i])/area2;
    return result;
}

struct prepared_dent { scrp_vec3 center,inv_radius,delta; };

void displace(scrp_vertex& vertex,const prepared_dent *fields,uint32_t count,float weight,const triple& dw,float limit) {
    if (!weight || !count || !limit) return;
    triple shift{};
    std::array<triple,3> derivative{};
    bool affected=false;
    const auto p=vertex.position;
    for (uint32_t i=0;i<count;++i) {
        const auto& d=fields[i];
        const float x=(p.x-d.center.x)*d.inv_radius.x,y=(p.y-d.center.y)*d.inv_radius.y,z=(p.z-d.center.z)*d.inv_radius.z;
        const float t=1-(x*x+y*y+z*z);
        if (t<=0) continue;
        affected=true;
        const auto delta=coordinates(d.delta);
        const triple slope={-4*t*x*d.inv_radius.x,-4*t*y*d.inv_radius.y,-4*t*z*d.inv_radius.z};
        for (int k=0;k<3;++k) {
            shift[k]+=delta[k]*t*t;
            for (int j=0;j<3;++j) derivative[j][k]+=delta[k]*slope[j];
        }
    }
    if (!affected) return;
    for (int j=0;j<3;++j)
        for (int k=0;k<3;++k) derivative[j][k]=derivative[j][k]*weight+shift[k]*dw[j];
    for (auto& component:shift) component*=weight;
    const double length2=dot(shift,shift);
    if (length2>double(limit)*limit) {
        const double scale=limit/std::sqrt(length2);
        /* Radial projection at the displacement cap removes the radial derivative. */
        for (auto& column:derivative) {
            const double radial=dot(column,shift)/length2;
            for (int k=0;k<3;++k) column[k]=scale*(column[k]-shift[k]*radial);
        }
        for (auto& component:shift) component*=scale;
    }
    vertex.position={float(p.x+shift[0]),float(p.y+shift[1]),float(p.z+shift[2])};
    /* Transport the original shading normal by the local cofactor. Coincident
       smooth vertices remain smooth, while authored hard edges stay separate. */
    for (int j=0;j<3;++j) derivative[j][j]+=1;
    const auto a=cross(derivative[1],derivative[2]),b=cross(derivative[2],derivative[0]),c=cross(derivative[0],derivative[1]);
    const auto n=vertex.normal;
    triple normal;
    for (int k=0;k<3;++k) normal[k]=n.x*a[k]+n.y*b[k]+n.z*c[k];
    const double length=std::sqrt(dot(normal,normal));
    if (length>1e-15) vertex.normal={float(normal[0]/length),float(normal[1]/length),float(normal[2]/length)};
}
}}

extern "C" int scrp_mesh_dent_uv(const scrp_uv_vertex *source,const float *weights,uint32_t count,
                                 const scrp_dent *dents,uint32_t dent_count,float limit,
                                 scrp_uv_vertex *out,scrp_bounds3 *bounds,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!source || !out || !bounds || !count || count%3 || count>3000000 || dent_count>256 ||
            (dent_count && !dents) || !std::isfinite(limit) || limit<0 || limit>10000)
            return scrp::capi::fail(error,"Dents require triangle buffers, bounds and a finite displacement limit");
        scrp::prepared_dent fields[256];
        for (uint32_t i=0;i<dent_count;++i) {
            const auto& d=dents[i];
            if (!scrp::capi::bounded(d.center,1e6f) || !scrp::capi::bounded(d.radii,1e6f) ||
                !scrp::capi::bounded(d.displacement,1e6f) || std::min({d.radii.x,d.radii.y,d.radii.z})<1e-6f)
                return scrp::capi::fail(error,"Dent centers, radii and displacements must be finite; radii must be positive");
            fields[i]={d.center,{1/d.radii.x,1/d.radii.y,1/d.radii.z},d.displacement};
        }
        for (uint32_t i=0;i<count;++i) {
            const auto& v=source[i]; const auto color=v.vertex.color;
            if (!scrp::capi::bounded(v.vertex.position,1e6f) || !scrp::capi::finite(v.vertex.normal) ||
                !std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) || !std::isfinite(color.a) ||
                !std::isfinite(v.u) || !std::isfinite(v.v) || (weights && (!std::isfinite(weights[i]) || weights[i]<0 || weights[i]>1)))
                return scrp::capi::fail(error,"Invalid textured vertex or dent weight");
        }
        scrp_bounds3 b{source[0].vertex.position,source[0].vertex.position};
        for (uint32_t i=0;i<count;i+=3) {
            const auto dw=scrp::weight_gradient(source+i,weights?weights+i:nullptr);
            for (uint32_t j=i;j<i+3;++j) {
                out[j]=source[j];
                scrp::displace(out[j].vertex,fields,dent_count,weights?weights[j]:1,dw,limit);
                const auto p=out[j].vertex.position;
                if (!j) b={p,p};
                b.minimum={std::min(b.minimum.x,p.x),std::min(b.minimum.y,p.y),std::min(b.minimum.z,p.z)};
                b.maximum={std::max(b.maximum.x,p.x),std::max(b.maximum.y,p.y),std::max(b.maximum.z,p.z)};
            }
        }
        *bounds=b;
        return 1;
    });
}
