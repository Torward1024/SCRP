#include <scrp/c/Mesh.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s (%s)\n",__LINE__,#x,error.message); ++failures; } } while (0)
static void test_dents(void) {
    scrp_error error={{0}};
    const scrp_color paint={.7f,.3f,.2f,1};
    scrp_uv_vertex source[6]={
        {{{0,0,0},{0,0,1},paint},0,0},{{{.5f,0,0},{0,0,1},paint},.5f,0},{{{0,.5f,0},{0,0,1},paint},0,.5f},
        {{{.5f,0,0},{0,0,1},paint},0,1},{{{2,0,0},{.6f,0,.8f},paint},1,1},{{{2,1,0},{0,.8f,.6f},paint},1,0}
    };
    scrp_dent dents[2]={{{0,0,0},{1,1,1},{0,0,-.4f}},{{0,0,0},{1,1,1},{0,0,-.4f}}};
    scrp_uv_vertex out[6],previous[6];
    scrp_bounds3 bounds,old;
    CHECK(scrp_mesh_dent_uv(source,NULL,6,dents,1,1,out,&bounds,&error));
    CHECK(fabsf(out[0].vertex.position.z+.4f)<1e-6f && fabsf(out[1].vertex.position.z+.225f)<1e-6f);
    CHECK(!memcmp(&out[1].vertex,&out[3].vertex,sizeof(scrp_vertex)) && out[3].v==1 && out[1].u==.5f);
    const scrp_vec3 expected=scrp_vec3_normalize((scrp_vec3){-.6f,0,1});
    CHECK(scrp_vec3_dot(expected,out[1].vertex.normal)>.99999f);
    CHECK(!memcmp(source+4,out+4,2*sizeof(*out)) && bounds.minimum.z==-.4f && bounds.maximum.x==2);
    float weights[]={0,1,1,1,1,1};
    CHECK(scrp_mesh_dent_uv(source,weights,6,dents,2,.3f,out,&bounds,&error));
    CHECK(!memcmp(source,out,sizeof(*out)) && fabsf(out[1].vertex.position.z+.3f)<1e-6f && bounds.minimum.z==-.3f);
    memcpy(previous,out,sizeof(out)); old=bounds; dents[1].radii.y=NAN;
    CHECK(!scrp_mesh_dent_uv(source,weights,6,dents,2,1,out,&bounds,&error));
    CHECK(!memcmp(previous,out,sizeof(out)) && !memcmp(&old,&bounds,sizeof(old)));
    weights[5]=2; CHECK(!scrp_mesh_dent_uv(source,weights,6,dents,1,1,out,&bounds,&error));
    CHECK(!memcmp(previous,out,sizeof(out)));
    CHECK(scrp_mesh_dent_uv(source,NULL,6,NULL,0,1,out,&bounds,&error) && !memcmp(source,out,sizeof(out)));
    CHECK(scrp_mesh_dent_uv(out,NULL,6,dents,1,1,out,&bounds,&error) && out[0].vertex.position.z==-.4f);
}
int main(void) {
    scrp_error error={{0}};
    const scrp_vertex source[3]={{{0,0,0},{0,0,1},{.1f,.2f,.3f,1}},{{1,0,0},{0,0,1},{1,1,1,1}},{{0,1,0},{0,0,1},{1,1,1,1}}};
    scrp_vertex out[3],previous[3];
    scrp_vec3 delta[3]={{0,0,10},{0,0,10},{0,0,10}};
    float weights[3]={0,.05f,1}; scrp_bounds3 bounds,old_bounds;
    CHECK(scrp_mesh_deform(source,delta,weights,3,1,out,&bounds,&error));
    CHECK(out[0].position.z==0 && out[1].position.z==.5f && out[2].position.z==1);
    CHECK(fabsf(scrp_vec3_dot(out[0].normal,out[0].normal)-1)<.00001f && out[0].normal.z>.6f);
    CHECK(bounds.minimum.z==0 && bounds.maximum.z==1 && out[0].color.g==source[0].color.g);
    memcpy(previous,out,sizeof(out)); old_bounds=bounds; delta[2].x=NAN;
    CHECK(!scrp_mesh_deform(source,delta,NULL,3,1,out,&bounds,&error));
    CHECK(memcmp(previous,out,sizeof(out))==0 && memcmp(&old_bounds,&bounds,sizeof(bounds))==0);
    delta[2].x=0; weights[1]=-1;
    CHECK(!scrp_mesh_deform(source,delta,weights,3,1,out,&bounds,&error));
    /* Rebuilding from source recovers the original geometry without accumulated damage. */
    CHECK(scrp_mesh_deform(source,delta,NULL,3,0,out,&bounds,&error));
    CHECK(memcmp(source,out,sizeof(out))==0);
    CHECK(scrp_mesh_deform(out,delta,NULL,3,1,out,&bounds,&error));
    CHECK(out[0].position.z==1 && out[2].position.z==1);
    CHECK(!scrp_mesh_deform(source,delta,NULL,2,1,out,&bounds,&error));
    test_dents();
    printf("C17 mesh displacement, protected vertices, normals, bounds, recovery: failures=%d\n",failures);
    return failures?1:0;
}
