#include <scrp/c/Physics3D.h>
#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s (%s)\n",__LINE__,#x,error.message); ++failures; } } while (0)
static scrp_body_desc box(float x, float y, float mass) {
    scrp_body_desc d = {0};
    d.pose = (scrp_pose){{x,y,0},{0,0,0,1}};
    d.half_extent = (scrp_vec3){0.5f,0.5f,0.5f};
    d.mass = mass; d.friction = 0.7f; d.continuous = 1;
    return d;
}
int main(void) {
    scrp_error error = {{0}};
    scrp_world *world = NULL, *other = NULL;
    scrp_world_desc wd = {{0,-9.81f,0},64};
    CHECK(!scrp_world_create(NULL,&world,&error));
    CHECK(!world && error.message[0]);
    CHECK(scrp_world_create(&wd,&world,&error));
    if (!world) return 1;
    CHECK(scrp_world_create(&wd,&other,&error));
    CHECK(!scrp_world_create(&wd,&world,&error));
    scrp_body_id ground = 0, body = 0, light = 0;
    scrp_body_desc d = box(0,-0.5f,0);
    d.half_extent = (scrp_vec3){100,0.5f,100};
    CHECK(scrp_world_add_box(world,&d,&ground,&error));
    d = box(0,5,100);
    CHECK(scrp_world_add_box(world,&d,&body,&error));
    d = box(3,5,10);
    CHECK(scrp_world_add_box(world,&d,&light,&error));
    scrp_body_state state = {0}, lighter = {0};
    CHECK(!scrp_world_body_state(other,body,&state,&error));
    CHECK(!scrp_world_step(world,NAN,&error));
    CHECK(!scrp_world_step(world,1,&error));
    for (int i=0;i<30;++i) CHECK(scrp_world_step(world,1.0f/120,&error));
    CHECK(scrp_world_body_state(world,body,&state,&error));
    CHECK(scrp_world_body_state(world,light,&lighter,&error));
    CHECK(state.pose.position.y < 4.8f && state.pose.position.y > 4.6f);
    CHECK(fabsf(state.pose.position.y-lighter.pose.position.y)<0.001f);
    for (int i=0;i<1200;++i) CHECK(scrp_world_step(world,1.0f/120,&error));
    CHECK(scrp_world_body_state(world,body,&state,&error));
    printf("Rest pose y=%.6f, vertical speed=%.6f\n",state.pose.position.y,state.linear_velocity.y);
    CHECK(fabsf(state.pose.position.y-0.5f)<0.02f && fabsf(state.linear_velocity.y)<0.01f);
    CHECK(!state.active);
    const float resting = state.pose.position.y;
    for (int i=0;i<1200;++i) CHECK(scrp_world_step(world,1.0f/120,&error));
    CHECK(scrp_world_body_state(world,body,&state,&error));
    CHECK(fabsf(resting-state.pose.position.y)<0.001f);
    scrp_ray_hit hit;
    CHECK(scrp_world_raycast(world,(scrp_vec3){0,4,0},(scrp_vec3){0,-10,0},body,&hit,&error));
    CHECK(hit.hit && hit.body==ground && fabsf(hit.point.y)<0.001f && hit.normal.y>0.99f);
    CHECK(scrp_world_raycast(world,(scrp_vec3){0,4,0},(scrp_vec3){0,-10,0},0,&hit,&error));
    CHECK(hit.hit && hit.body==body && hit.point.y>0.9f);
    CHECK(scrp_world_raycast(world,(scrp_vec3){0,4,0},(scrp_vec3){0,10,0},0,&hit,&error));
    CHECK(!hit.hit && hit.body==0);
    CHECK(!scrp_world_raycast(world,(scrp_vec3){NAN,0,0},(scrp_vec3){0,-1,0},0,&hit,&error));
    /* A finite-width sweep supports an edge which a center ray misses. */
    scrp_cylinder_cast cast={0}; scrp_shape_hit shape_hit;
    cast.pose=(scrp_pose){{0,1,0},scrp_quat_axis_angle((scrp_vec3){0,0,1},-1.570796327f)};
    cast.radius=.35f; cast.half_height=.12f; cast.displacement=(scrp_vec3){0,-2,0};
    cast.surface_axis=(scrp_vec3){0,1,0}; cast.min_surface_dot=.3f; cast.ignore=body;
    CHECK(scrp_world_cast_cylinder(world,&cast,&shape_hit,&error));
    printf("Cylinder floor hit: fraction %.6f, point %.6f %.6f %.6f, normal %.6f %.6f %.6f, depth %.6f\n",shape_hit.surface.fraction,
        shape_hit.surface.point.x,shape_hit.surface.point.y,shape_hit.surface.point.z,shape_hit.surface.normal.x,shape_hit.surface.normal.y,shape_hit.surface.normal.z,shape_hit.penetration_depth);
    CHECK(shape_hit.surface.hit && shape_hit.surface.body==ground && fabsf(shape_hit.surface.fraction-.325f)<.002f);
    CHECK(shape_hit.surface.normal.y>.99f && fabsf(shape_hit.surface.point.y)<.002f && shape_hit.penetration_depth==0);
    cast.pose.position.y=.25f;
    CHECK(scrp_world_cast_cylinder(world,&cast,&shape_hit,&error));
    CHECK(shape_hit.surface.hit && shape_hit.surface.fraction==0 && fabsf(shape_hit.penetration_depth-.10f)<.003f);
    scrp_body_id edge;
    d=box(20,.75f,0); d.half_extent=(scrp_vec3){.1f,.05f,1};
    CHECK(scrp_world_add_box(world,&d,&edge,&error));
    cast.pose.position=(scrp_vec3){20.2f,1.5f,0};
    CHECK(scrp_world_raycast(world,cast.pose.position,cast.displacement,body,&hit,&error) && hit.body==ground);
    CHECK(scrp_world_cast_cylinder(world,&cast,&shape_hit,&error));
    CHECK(shape_hit.surface.hit && shape_hit.surface.body==edge && shape_hit.surface.normal.y>.9f);
    cast.surface_axis=(scrp_vec3){0,-1,0};
    CHECK(scrp_world_cast_cylinder(world,&cast,&shape_hit,&error) && !shape_hit.surface.hit);
    cast.surface_axis=(scrp_vec3){0,2,0};
    CHECK(!scrp_world_cast_cylinder(world,&cast,&shape_hit,&error));
    cast.surface_axis=(scrp_vec3){0,1,0}; cast.radius=NAN;
    CHECK(!scrp_world_cast_cylinder(world,&cast,&shape_hit,&error));

    /* Force at an offset generates both translation and rotation; reset removes both. */
    CHECK(scrp_world_set_pose(world,body,(scrp_pose){{0,8,0},{0,0,0,1}},&error));
    CHECK(scrp_world_apply_impulse(world,body,(scrp_vec3){100,0,0},(scrp_vec3){0,9,0},&error));
    CHECK(scrp_world_body_state(world,body,&state,&error));
    CHECK(fabsf(state.linear_velocity.x-1)<0.001f && fabsf(state.angular_velocity.z)>1);
    CHECK(scrp_world_apply_force(world,body,(scrp_vec3){10000,0,0},(scrp_vec3){0,8,0},&error));
    CHECK(scrp_world_set_pose(world,body,(scrp_pose){{0,8,0},{0,0,0,1}},&error));
    CHECK(scrp_world_step(world,1.0f/120,&error));
    CHECK(scrp_world_body_state(world,body,&state,&error));
    CHECK(fabsf(state.linear_velocity.x)<0.001f && fabsf(state.angular_velocity.z)<0.001f);
    CHECK(scrp_world_remove_body(world,body,&error));
    CHECK(!scrp_world_body_state(world,body,&state,&error));
    CHECK(!scrp_world_remove_body(world,body,&error));
    /* A fast continuous body must not tunnel through the static floor. */
    d=box(10,5,10);
    const scrp_body_id stale=body;
    CHECK(scrp_world_add_box(world,&d,&body,&error));
    CHECK(body!=stale && !scrp_world_body_state(world,stale,&state,&error));
    CHECK(scrp_world_apply_impulse(world,body,(scrp_vec3){0,-3000,0},d.pose.position,&error));
    for (int i=0;i<4;++i) CHECK(scrp_world_step(world,1.0f/30,&error));
    CHECK(scrp_world_body_state(world,body,&state,&error));
    CHECK(state.pose.position.y>.45f && state.pose.position.y<.6f);
    CHECK(!scrp_world_apply_force(world,body,(scrp_vec3){1e30f,0,0},d.pose.position,&error));
    d.mass=1e-35f;
    scrp_body_id invalid=99;
    CHECK(!scrp_world_add_box(world,&d,&invalid,&error) && !invalid);
    d.mass=10;
    d.pose.rotation.w = 0; d.pose.rotation.x = 0;
    CHECK(!scrp_world_add_box(world,&d,&body,&error) && body==0);
    scrp_world_destroy(&world);
    CHECK(!world);
    /* Destroying one world must not invalidate another world's backend. */
    d = box(0,2,10);
    CHECK(scrp_world_add_box(other,&d,&body,&error));
    CHECK(scrp_world_step(other,1.0f/120,&error));
    scrp_world_destroy(&other);
    scrp_world_destroy(&world);
    scrp_world_destroy(NULL);
    /* Rotation, composition, and q/-q interpolation agree across the C ABI. */
    const scrp_quat yaw=scrp_quat_axis_angle((scrp_vec3){0,1,0},1.570796327f);
    const scrp_vec3 rotated=scrp_quat_rotate(yaw,(scrp_vec3){0,0,1});
    CHECK(fabsf(rotated.x-1)<.00001f && fabsf(rotated.z)<.00001f);
    const scrp_pose halfway=scrp_pose_lerp((scrp_pose){{0,0,0},yaw},(scrp_pose){{2,0,0},{-yaw.x,-yaw.y,-yaw.z,-yaw.w}},.5f);
    CHECK(fabsf(halfway.position.x-1)<.00001f && fabsf(halfway.rotation.y-yaw.y)<.00001f);
    printf("Physics C17 including cylinder sweeps, overlap and edge support: %s; failures=%d\n",scrp_physics_backend(),failures);
    return failures ? 1 : 0;
}
