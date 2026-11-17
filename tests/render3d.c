#include <scrp/c/Render3D.h>
#include <math.h>
#include <stdio.h>
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s (%s)\n",__LINE__,#x,error.message); ++failures; } } while (0)
int main(void) {
    scrp_error error={{0}}; scrp_view3d *view=NULL,*other=NULL;
    const scrp_view_desc desc={"SCRP 3D contract",320,180,1,0};
    CHECK(!scrp_view3d_create(NULL,&view,&error));
    CHECK(view==NULL);
    CHECK(scrp_view3d_create(&desc,&view,&error));
    if (!view) return 1;
    CHECK(scrp_view3d_create(&desc,&other,&error));
    CHECK(!scrp_view3d_create(&desc,&view,&error));
    scrp_mesh_id cube=0; CHECK(scrp_view3d_box_mesh(view,&cube,&error));
    CHECK(!scrp_view3d_mesh_destroy(other,cube,&error));
    const scrp_pose pose={{0,0,0},{0,0,0,1}};
    const scrp_vec3 scale={1,1,1}; const scrp_color color={.5f,.6f,.7f,1};
    CHECK(!scrp_view3d_draw(view,cube,pose,scale,color,0,&error));
    scrp_camera camera={0};
    camera.eye=(scrp_vec3){2,2,3}; camera.target=(scrp_vec3){0,0,0}; camera.up=(scrp_vec3){0,1,0};
    camera.vertical_fov=1; camera.near_plane=.1f; camera.far_plane=100; camera.fog_end=50;
    CHECK(scrp_view3d_begin(view,&camera,&error));
    CHECK(!scrp_view3d_begin(view,&camera,&error));
    CHECK(scrp_view3d_draw(view,cube,pose,scale,color,0,&error));
    CHECK(!scrp_view3d_draw(view,cube,pose,(scrp_vec3){0,1,1},color,0,&error));
    CHECK(scrp_view3d_text(view,10,10,"C17 OPENGL CONTRACT",2,color,320,180,&error));
    CHECK(!scrp_view3d_draw(view,cube,pose,scale,color,0,&error));
    CHECK(scrp_view3d_present(view,NULL,&error));
    CHECK(!scrp_view3d_present(view,NULL,&error));
    CHECK(scrp_view3d_mesh_destroy(view,cube,&error));
    CHECK(!scrp_view3d_mesh_destroy(view,cube,&error));
    scrp_view3d_destroy(&view);
    /* The second context remains usable after destroying the first. */
    CHECK(scrp_view3d_box_mesh(other,&cube,&error));
    CHECK(scrp_view3d_begin(other,&camera,&error));
    CHECK(scrp_view3d_draw(other,cube,pose,scale,color,0,&error));
    CHECK(scrp_view3d_present(other,NULL,&error));
    scrp_view3d_destroy(&other); scrp_view3d_destroy(&view); scrp_view3d_destroy(NULL);
    printf("OpenGL C17 lifecycle, ownership, frame order: failures=%d\n",failures);
    return failures?1:0;
}
