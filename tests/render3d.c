#include <scrp/c/Render3D.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s (%s)\n",__LINE__,#x,error.message); ++failures; } } while (0)
static int write_test_atlas(const char *path) {
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,2,2,32,SDL_PIXELFORMAT_RGBA32);
    if (!surface) return 0;
    const Uint8 colors[4][3]={{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
    for (int i=0;i<4;++i) {
        SDL_Rect pixel={i%2,i/2,1,1};
        SDL_FillRect(surface,&pixel,SDL_MapRGB(surface->format,colors[i][0],colors[i][1],colors[i][2]));
    }
    const int result=SDL_SaveBMP(surface,path);
    SDL_FreeSurface(surface);
    return result==0;
}
static int pixel_matches(SDL_Surface *surface,int x,int y,int r,int g,int b) {
    const Uint8 *pixel=(const Uint8*)surface->pixels+y*surface->pitch+4*x;
    return abs(pixel[0]-r)<50 && abs(pixel[1]-g)<50 && abs(pixel[2]-b)<50;
}
static void test_atlas(scrp_view3d *view,scrp_view3d *other,scrp_mesh_id plain) {
    scrp_error error={{0}};
    const char *atlas_path="scrp-render3d-test-atlas.bmp",*frame_path="scrp-render3d-test-frame.bmp";
    const scrp_color white={1,1,1,1};
    const scrp_pose pose={{0,0,0},{0,0,0,1}};
    const scrp_vec3 scale={1,1,1};
    const scrp_vec3 corners[]={{-2,1,0},{2,1,0},{2,-1,0},{-2,-1,0}};
    const int order[]={0,1,2,0,2,3};
    scrp_uv_vertex quad[6];
    for (int i=0;i<6;++i) {
        int j=order[i];
        quad[i]=(scrp_uv_vertex){{corners[j],{0,0,1},white},j==1||j==2,j>=2};
    }
    scrp_mesh_id mesh=0; scrp_texture_id texture=0;
    CHECK(write_test_atlas(atlas_path));
    CHECK(scrp_view3d_texture_load_bmp(view,atlas_path,&texture,&error));
    CHECK(!scrp_view3d_texture_destroy(other,texture,&error));
    CHECK(scrp_view3d_uv_mesh_create(view,quad,6,&mesh,&error));
    scrp_mesh_id invalid=99;
    quad[0].v=NAN;
    CHECK(!scrp_view3d_uv_mesh_create(view,quad,6,&invalid,&error) && invalid==0);
    const scrp_vertex ordinary_triangle[3]={quad[0].vertex,quad[1].vertex,quad[2].vertex};
    CHECK(!scrp_view3d_mesh_update(view,mesh,ordinary_triangle,3,&error));
    scrp_camera camera={0};
    camera.eye=(scrp_vec3){0,0,2}; camera.up=(scrp_vec3){0,1,0};
    camera.vertical_fov=1; camera.near_plane=.1f; camera.far_plane=30;
    camera.fog_start=10; camera.fog_end=20;
    CHECK(scrp_view3d_begin(view,&camera,&error));
    CHECK(!scrp_view3d_draw_textured(view,plain,texture,pose,scale,white,1,&error));
    CHECK(!scrp_view3d_draw_textured(view,mesh,0,pose,scale,white,1,&error));
    CHECK(scrp_view3d_draw_textured(view,mesh,texture,pose,scale,white,1,&error));
    /* A subsequent untextured draw must use its own material. */
    const scrp_pose foreground={{0,0,.2f},{0,0,0,1}};
    CHECK(scrp_view3d_draw(view,plain,foreground,(scrp_vec3){.3f,.3f,.1f},white,1,&error));
    CHECK(scrp_view3d_present(view,frame_path,&error));
    SDL_Surface *capture=SDL_LoadBMP(frame_path);
    SDL_Surface *rgba=capture?SDL_ConvertSurfaceFormat(capture,SDL_PIXELFORMAT_RGBA32,0):NULL;
    CHECK(rgba!=NULL);
    if (rgba) {
        CHECK(pixel_matches(rgba,rgba->w/4,rgba->h/4,255,0,0));
        CHECK(pixel_matches(rgba,3*rgba->w/4,rgba->h/4,0,255,0));
        CHECK(pixel_matches(rgba,rgba->w/4,3*rgba->h/4,0,0,255));
        CHECK(pixel_matches(rgba,3*rgba->w/4,3*rgba->h/4,255,255,255));
        CHECK(pixel_matches(rgba,rgba->w/2,rgba->h/2,255,255,255));
    }
    SDL_FreeSurface(rgba); SDL_FreeSurface(capture);
    CHECK(scrp_view3d_texture_destroy(view,texture,&error));
    CHECK(!scrp_view3d_texture_destroy(view,texture,&error));
    CHECK(scrp_view3d_mesh_destroy(view,mesh,&error));
    remove(atlas_path); remove(frame_path);
}
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
    test_atlas(view,other,cube);
    scrp_vertex triangle[3]={{{0,0,0},{0,0,1},{1,1,1,1}},{{1,0,0},{0,0,1},{1,1,1,1}},{{0,1,0},{0,0,1},{1,1,1,1}}};
    scrp_mesh_id dynamic=0;
    CHECK(scrp_view3d_mesh_create(view,triangle,3,&dynamic,&error));
    triangle[2].position.y=2;
    CHECK(!scrp_view3d_mesh_update(other,dynamic,triangle,3,&error));
    CHECK(scrp_view3d_mesh_update(view,dynamic,triangle,3,&error));
    triangle[2].position.y=NAN;
    CHECK(!scrp_view3d_mesh_update(view,dynamic,triangle,3,&error));
    triangle[2].position.y=2;
    CHECK(!scrp_view3d_mesh_update(view,dynamic,triangle,2,&error));
    const scrp_pose pose={{0,0,0},{0,0,0,1}};
    const scrp_vec3 scale={1,1,1}; const scrp_color color={.5f,.6f,.7f,1};
    CHECK(!scrp_view3d_draw(view,cube,pose,scale,color,0,&error));
    scrp_camera camera={0};
    camera.eye=(scrp_vec3){2,2,3}; camera.target=(scrp_vec3){0,0,0}; camera.up=(scrp_vec3){0,1,0};
    camera.vertical_fov=1; camera.near_plane=.1f; camera.far_plane=100; camera.fog_end=50;
    CHECK(scrp_view3d_begin(view,&camera,&error));
    CHECK(!scrp_view3d_begin(view,&camera,&error));
    CHECK(scrp_view3d_draw(view,cube,pose,scale,color,0,&error));
    CHECK(scrp_view3d_draw(view,dynamic,pose,scale,color,0,&error));
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
