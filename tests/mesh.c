#include <scrp/c/Mesh.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s (%s)\n",__LINE__,#x,error.message); ++failures; } } while (0)
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
    printf("C17 mesh displacement, protected vertices, normals, bounds, recovery: failures=%d\n",failures);
    return failures?1:0;
}
