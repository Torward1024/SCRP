#include <scrp/c/Render3D.h>
#include <scrp/MicroFont.h>
#include "CApiInternal.h"
#include <SDL.h>
#ifdef SCRP_RENDER3D_PNG
#include <png.h>
#endif
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace scrp { namespace {
struct GL {
#define SCRP_GL_FUNCTIONS(X) \
    X(PFNGLCREATESHADERPROC,CreateShader) X(PFNGLSHADERSOURCEPROC,ShaderSource) \
    X(PFNGLCOMPILESHADERPROC,CompileShader) X(PFNGLGETSHADERIVPROC,GetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC,GetShaderInfoLog) X(PFNGLDELETESHADERPROC,DeleteShader) \
    X(PFNGLCREATEPROGRAMPROC,CreateProgram) X(PFNGLATTACHSHADERPROC,AttachShader) \
    X(PFNGLLINKPROGRAMPROC,LinkProgram) X(PFNGLGETPROGRAMIVPROC,GetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC,GetProgramInfoLog) X(PFNGLDELETEPROGRAMPROC,DeleteProgram) \
    X(PFNGLUSEPROGRAMPROC,UseProgram) X(PFNGLGENVERTEXARRAYSPROC,GenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC,BindVertexArray) X(PFNGLDELETEVERTEXARRAYSPROC,DeleteVertexArrays) \
    X(PFNGLGENBUFFERSPROC,GenBuffers) X(PFNGLBINDBUFFERPROC,BindBuffer) \
    X(PFNGLBUFFERDATAPROC,BufferData) X(PFNGLDELETEBUFFERSPROC,DeleteBuffers) X(PFNGLGENERATEMIPMAPPROC,GenerateMipmap) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC,EnableVertexAttribArray) X(PFNGLVERTEXATTRIBPOINTERPROC,VertexAttribPointer) \
    X(PFNGLGETUNIFORMLOCATIONPROC,GetUniformLocation) X(PFNGLUNIFORMMATRIX4FVPROC,UniformMatrix4fv) \
    X(PFNGLUNIFORM4FPROC,Uniform4f) X(PFNGLUNIFORM3FPROC,Uniform3f) X(PFNGLUNIFORM1FPROC,Uniform1f) X(PFNGLUNIFORM1IPROC,Uniform1i)
#define DECL(type,name) type name = nullptr;
    SCRP_GL_FUNCTIONS(DECL)
#undef DECL
    void load() {
#define LOAD(type,name) name = reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name)); if (!name) throw std::runtime_error("Missing OpenGL entry point: gl" #name);
        SCRP_GL_FUNCTIONS(LOAD)
#undef LOAD
    }
};
using Matrix = std::array<float,16>;
Matrix identity() { return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; }
Matrix multiply(const Matrix& a,const Matrix& b) {
    Matrix c{};
    for (int col=0;col<4;++col) for (int row=0;row<4;++row)
        for (int k=0;k<4;++k) c[col*4+row]+=a[k*4+row]*b[col*4+k];
    return c;
}
Matrix model(scrp_pose pose,scrp_vec3 scale) {
    Matrix m = identity();
    const scrp_vec3 axes[3] = {scrp_quat_rotate(pose.rotation,{scale.x,0,0}),scrp_quat_rotate(pose.rotation,{0,scale.y,0}),scrp_quat_rotate(pose.rotation,{0,0,scale.z})};
    for (int i=0;i<3;++i) { m[i*4]=axes[i].x; m[i*4+1]=axes[i].y; m[i*4+2]=axes[i].z; }
    m[12]=pose.position.x; m[13]=pose.position.y; m[14]=pose.position.z;
    return m;
}
Matrix camera_matrix(const scrp_camera& c,float aspect) {
    const auto forward = scrp_vec3_normalize(scrp_vec3_sub(c.target,c.eye));
    const auto side = scrp_vec3_normalize(scrp_vec3_cross(forward,c.up));
    const auto up = scrp_vec3_cross(side,forward);
    Matrix view{side.x,up.x,-forward.x,0, side.y,up.y,-forward.y,0, side.z,up.z,-forward.z,0,
                -scrp_vec3_dot(side,c.eye),-scrp_vec3_dot(up,c.eye),scrp_vec3_dot(forward,c.eye),1};
    Matrix projection{};
    const float f = 1.0f/std::tan(c.vertical_fov*0.5f);
    projection[0]=f/aspect; projection[5]=f;
    projection[10]=(c.far_plane+c.near_plane)/(c.near_plane-c.far_plane);
    projection[11]=-1; projection[14]=2*c.far_plane*c.near_plane/(c.near_plane-c.far_plane);
    return multiply(projection,view);
}
const char *vertex_source = R"GLSL(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=3) in vec2 uv;
uniform mat4 model, vp;
out vec3 world, n;
out vec4 vertex_color;
out vec2 texture_uv;
void main() {
    vec4 p = model * vec4(position,1.0);
    world=p.xyz; n=transpose(inverse(mat3(model)))*normal;
    vertex_color=color; texture_uv=uv; gl_Position=vp*p;
})GLSL";
const char *fragment_source = R"GLSL(#version 330 core
in vec3 world, n;
in vec4 vertex_color;
in vec2 texture_uv;
uniform sampler2D albedo;
uniform int textured;
uniform vec4 tint, fog_color;
uniform vec3 eye;
uniform float fog_start, fog_end, emissive;
uniform int overlay;
uniform vec4 light_position[2], light_direction[2], light_color[2];
out vec4 output_color;
void main() {
    vec4 surface=vertex_color*tint;
    if(overlay!=0) { output_color=surface; return; }
    if(textured!=0) surface*=texture(albedo,texture_uv);
    vec3 normal=normalize(n);
    float diffuse=max(dot(normal,normalize(vec3(-0.4,0.8,0.3))),0.0);
    vec3 lit=surface.rgb*(0.33+0.67*diffuse);
    for(int i=0;i<2;i++) {
        vec3 delta=world-light_position[i].xyz;
        float distance=length(delta);
        float range=light_position[i].w;
        if(range>0.0 && distance<range) {
            float cone=dot(delta/max(distance,0.001),normalize(light_direction[i].xyz));
            float beam=smoothstep(light_direction[i].w, min(1.0,light_direction[i].w+0.08),cone);
            float falloff=1.0-distance/range;
            lit+=surface.rgb*light_color[i].rgb*light_color[i].a*beam*falloff*max(dot(normal,-delta/max(distance,0.001)),0.0);
        }
    }
    lit=mix(lit,surface.rgb,emissive);
    float fog=clamp((length(world-eye)-fog_start)/(fog_end-fog_start),0.0,1.0);
    output_color=vec4(mix(lit,fog_color.rgb,fog),surface.a);
})GLSL";
std::atomic<uint64_t> next_resource{1};
struct Mesh { GLuint vao=0, vbo=0; GLsizei count=0; bool has_uv=false; };
bool color_ok(scrp_color c) { return std::isfinite(c.r)&&std::isfinite(c.g)&&std::isfinite(c.b)&&std::isfinite(c.a); }
}}

struct scrp_view3d {
    scrp::GL gl;
    SDL_Window *window=nullptr;
    SDL_GLContext context=nullptr;
    GLuint program=0;
    scrp::Mesh stream;
    std::unordered_map<scrp_mesh_id,scrp::Mesh> meshes;
    std::unordered_map<scrp_texture_id,GLuint> textures;
    bool video=false, frame=false, overlay=false;
    int width=0,height=0;
    Uint64 previous=0;
    std::string driver;
    ~scrp_view3d() {
        if (context) {
            SDL_GL_MakeCurrent(window,context);
            for (auto item:meshes) release(item.second);
            for (auto item:textures) glDeleteTextures(1,&item.second);
            release(stream);
            if (program) gl.DeleteProgram(program);
            SDL_GL_DeleteContext(context);
        }
        if (window) SDL_DestroyWindow(window);
        if (video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    void current() { if (SDL_GL_GetCurrentContext()!=context && SDL_GL_MakeCurrent(window,context)!=0) throw std::runtime_error(SDL_GetError()); }
    void release(scrp::Mesh& mesh) {
        if (mesh.vbo && gl.DeleteBuffers) gl.DeleteBuffers(1,&mesh.vbo);
        if (mesh.vao && gl.DeleteVertexArrays) gl.DeleteVertexArrays(1,&mesh.vao);
    }
    GLint uniform(const char *name) { return gl.GetUniformLocation(program,name); }
    void matrix(const char *name,const scrp::Matrix& value) { gl.UniformMatrix4fv(uniform(name),1,GL_FALSE,value.data()); }
    void color(const char *name,scrp_color c) { gl.Uniform4f(uniform(name),c.r,c.g,c.b,c.a); }
    void upload(scrp::Mesh& mesh,const void *vertices,uint32_t count,GLenum usage) {
        const GLsizei stride=mesh.has_uv ? sizeof(scrp_uv_vertex) : sizeof(scrp_vertex);
        if (!mesh.vao) gl.GenVertexArrays(1,&mesh.vao);
        if (!mesh.vbo) gl.GenBuffers(1,&mesh.vbo);
        gl.BindVertexArray(mesh.vao); gl.BindBuffer(GL_ARRAY_BUFFER,mesh.vbo);
        gl.BufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(stride)*count,vertices,usage);
        gl.EnableVertexAttribArray(0); gl.EnableVertexAttribArray(1); gl.EnableVertexAttribArray(2);
        gl.VertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(scrp_vertex,position)));
        gl.VertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(scrp_vertex,normal)));
        gl.VertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(scrp_vertex,color)));
        if (mesh.has_uv) {
            gl.EnableVertexAttribArray(3);
            gl.VertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(scrp_uv_vertex,u)));
        }
        mesh.count=static_cast<GLsizei>(count);
        if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("OpenGL mesh upload failed");
    }
    scrp_mesh_id create_mesh(const void *vertices,uint32_t count,bool has_uv) {
        current();
        const auto id=scrp::next_resource.fetch_add(1);
        auto& mesh=meshes[id];
        mesh.has_uv=has_uv;
        try { upload(mesh,vertices,count,GL_STATIC_DRAW); }
        catch (...) { release(mesh); meshes.erase(id); throw; }
        return id;
    }
    GLuint shader(GLenum type,const char *source) {
        const GLuint id=gl.CreateShader(type);
        gl.ShaderSource(id,1,&source,nullptr); gl.CompileShader(id);
        GLint good=0; gl.GetShaderiv(id,GL_COMPILE_STATUS,&good);
        if (!good) { char log[1024]{}; gl.GetShaderInfoLog(id,sizeof(log),nullptr,log); gl.DeleteShader(id); throw std::runtime_error(log); }
        return id;
    }
};
static int upload_rgba(scrp_view3d *view,const void *pixels,int width,int height,int row_length,
                       scrp_texture_id *out,scrp_error *error) {
    view->current();
    const auto id=scrp::next_resource.fetch_add(1);
    auto& name=view->textures[id];
    glGenTextures(1,&name);
    glBindTexture(GL_TEXTURE_2D,name);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,row_length);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    view->gl.GenerateMipmap(GL_TEXTURE_2D);
    if (glGetError()!=GL_NO_ERROR) {
        glDeleteTextures(1,&name); view->textures.erase(id);
        return scrp::capi::fail(error,"OpenGL texture upload failed");
    }
    *out=id;
    return 1;
}
extern "C" {
int scrp_view3d_create(const scrp_view_desc *desc,scrp_view3d **out,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!desc || !out || *out || !desc->title || desc->width<320 || desc->height<180 || desc->width>7680 || desc->height>4320)
            return scrp::capi::fail(error,"View requires a title, valid dimensions and an empty output handle");
        auto view=std::make_unique<scrp_view3d>();
        SDL_SetMainReady();
        if (SDL_InitSubSystem(SDL_INIT_VIDEO)!=0) return scrp::capi::fail(error,SDL_GetError());
        view->video=true;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1); SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
        view->window=SDL_CreateWindow(desc->title,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,desc->width,desc->height,
            SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|(desc->hidden?SDL_WINDOW_HIDDEN:SDL_WINDOW_SHOWN));
        if (!view->window) return scrp::capi::fail(error,SDL_GetError());
        view->context=SDL_GL_CreateContext(view->window);
        if (!view->context) return scrp::capi::fail(error,SDL_GetError());
        view->gl.load();
        SDL_GL_SetSwapInterval(desc->vsync ? 1 : 0);
        view->driver=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        auto& gl=view->gl;
        const auto vs=view->shader(GL_VERTEX_SHADER,scrp::vertex_source);
        GLuint fs=0;
        try { fs=view->shader(GL_FRAGMENT_SHADER,scrp::fragment_source); }
        catch (...) { gl.DeleteShader(vs); throw; }
        view->program=gl.CreateProgram(); gl.AttachShader(view->program,vs); gl.AttachShader(view->program,fs);
        gl.LinkProgram(view->program); gl.DeleteShader(vs); gl.DeleteShader(fs);
        GLint good=0; gl.GetProgramiv(view->program,GL_LINK_STATUS,&good);
        if (!good) { char log[1024]{}; gl.GetProgramInfoLog(view->program,sizeof(log),nullptr,log); return scrp::capi::fail(error,log); }
        view->previous=SDL_GetPerformanceCounter();
        *out=view.release();
        return 1;
    });
}
void scrp_view3d_destroy(scrp_view3d **view) { if (view && *view) { delete *view; *view=nullptr; } }
int scrp_view3d_poll(scrp_view3d *view,scrp_view_input *out,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!view || !out) return scrp::capi::fail(error,"View and input output are required");
        *out={};
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type==SDL_QUIT || (event.type==SDL_WINDOWEVENT && event.window.windowID==SDL_GetWindowID(view->window) && event.window.event==SDL_WINDOWEVENT_CLOSE)) out->quit=1;
        }
        out->focused=(SDL_GetWindowFlags(view->window)&SDL_WINDOW_INPUT_FOCUS)!=0;
        SDL_GL_GetDrawableSize(view->window,&out->width,&out->height);
        const auto now=SDL_GetPerformanceCounter();
        out->elapsed_seconds=double(now-view->previous)/SDL_GetPerformanceFrequency(); view->previous=now;
        return 1;
    });
}
int scrp_view3d_key_down(const scrp_view3d *view,const char *name) {
    if (!view || !name || !(SDL_GetWindowFlags(view->window)&SDL_WINDOW_INPUT_FOCUS)) return 0;
    const auto code=SDL_GetScancodeFromName(name);
    return code!=SDL_SCANCODE_UNKNOWN && SDL_GetKeyboardState(nullptr)[code]!=0;
}
int scrp_view3d_mesh_create(scrp_view3d *view,const scrp_vertex *vertices,uint32_t count,scrp_mesh_id *out,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (out) *out=0;
        if (!view || !vertices || !out || !count || count%3 || count>3000000) return scrp::capi::fail(error,"Mesh requires triangle vertices and an output");
        for (uint32_t i=0;i<count;++i)
            if (!scrp::capi::finite(vertices[i].position) || !scrp::capi::finite(vertices[i].normal) || !scrp::color_ok(vertices[i].color)) return scrp::capi::fail(error,"Mesh vertices must be finite");
        *out=view->create_mesh(vertices,count,false);
        return 1;
    });
}
int scrp_view3d_uv_mesh_create(scrp_view3d *view,const scrp_uv_vertex *vertices,uint32_t count,scrp_mesh_id *out,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (out) *out=0;
        if (!view || !vertices || !out || !count || count%3 || count>3000000)
            return scrp::capi::fail(error,"UV mesh requires triangle vertices and an output");
        for (uint32_t i=0;i<count;++i) {
            const auto& v=vertices[i].vertex;
            if (!scrp::capi::finite(v.position) || !scrp::capi::finite(v.normal) || !scrp::color_ok(v.color) ||
                !std::isfinite(vertices[i].u) || !std::isfinite(vertices[i].v))
                return scrp::capi::fail(error,"UV mesh vertices and coordinates must be finite");
        }
        *out=view->create_mesh(vertices,count,true);
        return 1;
    });
}
int scrp_view3d_texture_load_bmp(scrp_view3d *view,const char *path,scrp_texture_id *out,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (out) *out=0;
        if (!view || !path || !*path || !out) return scrp::capi::fail(error,"Texture requires a BMP path and an output");
        using Surface=std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)>;
        Surface bitmap(SDL_LoadBMP(path),SDL_FreeSurface);
        if (!bitmap) return scrp::capi::fail(error,SDL_GetError());
        if (bitmap->w>8192 || bitmap->h>8192) return scrp::capi::fail(error,"Texture dimensions exceed 8192 pixels");
        Surface rgba(SDL_ConvertSurfaceFormat(bitmap.get(),SDL_PIXELFORMAT_RGBA32,0),SDL_FreeSurface);
        if (!rgba) return scrp::capi::fail(error,SDL_GetError());
        return upload_rgba(view,rgba->pixels,rgba->w,rgba->h,rgba->pitch/4,out,error);
    });
}
int scrp_view3d_texture_load_png(scrp_view3d *view,const uint8_t *bytes,size_t size,scrp_texture_id *out,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (out) *out=0;
        if (!view || !bytes || !size || !out) return scrp::capi::fail(error,"Texture requires encoded PNG bytes and an output");
#ifdef SCRP_RENDER3D_PNG
        png_image image{};
        image.version=PNG_IMAGE_VERSION;
        auto cleanup=std::unique_ptr<png_image,decltype(&png_image_free)>(&image,png_image_free);
        if (!png_image_begin_read_from_memory(&image,bytes,size)) return scrp::capi::fail(error,image.message);
        if (image.width>8192 || image.height>8192) return scrp::capi::fail(error,"Texture dimensions exceed 8192 pixels");
        image.format=PNG_FORMAT_RGBA;
        std::vector<uint8_t> rgba(PNG_IMAGE_SIZE(image));
        if (!png_image_finish_read(&image,nullptr,rgba.data(),0,nullptr)) return scrp::capi::fail(error,image.message);
        return upload_rgba(view,rgba.data(),int(image.width),int(image.height),0,out,error);
#else
        return scrp::capi::fail(error,"PNG textures require SCRP_RENDER3D_PNG");
#endif
    });
}
int scrp_view3d_texture_destroy(scrp_view3d *view,scrp_texture_id id,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!view || !view->textures.count(id)) return scrp::capi::fail(error,"Unknown texture in this view");
        view->current();
        glDeleteTextures(1,&view->textures.at(id));
        view->textures.erase(id);
        return 1;
    });
}
int scrp_view3d_mesh_destroy(scrp_view3d *view,scrp_mesh_id mesh,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!view || !view->meshes.count(mesh)) return scrp::capi::fail(error,"Unknown mesh in this view");
        view->current(); view->release(view->meshes.at(mesh)); view->meshes.erase(mesh); return 1;
    });
}
int scrp_view3d_mesh_update(scrp_view3d *view,scrp_mesh_id id,const scrp_vertex *v,uint32_t count,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!view || !view->meshes.count(id) || view->meshes.at(id).has_uv || !v || !count || count%3 || count>3000000)
            return scrp::capi::fail(error,"Mesh update requires an owned mesh and triangles");
        for (uint32_t i=0;i<count;++i) if (!scrp::capi::finite(v[i].position) || !scrp::capi::finite(v[i].normal) || !scrp::color_ok(v[i].color))
            return scrp::capi::fail(error,"Mesh vertices must be finite");
        view->current(); view->upload(view->meshes.at(id),v,count,GL_DYNAMIC_DRAW); return 1;
    });
}
int scrp_view3d_box_mesh(scrp_view3d *view,scrp_mesh_id *out,scrp_error *error) {
    const scrp_vec3 p[8]={{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},
                          {-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}};
    const int faces[6][4]={{4,5,6,7},{1,0,3,2},{0,4,7,3},{5,1,2,6},{3,7,6,2},{0,1,5,4}};
    const scrp_vec3 n[6]={{0,0,1},{0,0,-1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0}};
    const int indices[6]={0,1,2,0,2,3}; scrp_vertex vertices[36];
    for (int f=0;f<6;++f) for (int i=0;i<6;++i) vertices[f*6+i]={p[faces[f][indices[i]]],n[f],{1,1,1,1}};
    return scrp_view3d_mesh_create(view,vertices,36,out,error);
}
int scrp_view3d_cylinder_mesh(scrp_view3d *view,uint32_t segments,scrp_mesh_id *out,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (segments<3 || segments>128) return scrp::capi::fail(error,"Cylinder segment count must be in 3..128");
        std::vector<scrp_vertex> v;
        for (uint32_t i=0;i<segments;++i) {
            const float a=6.283185307f*i/segments, b=6.283185307f*(i+1)/segments;
            const scrp_vec3 p[4]={{-.5f,std::cos(a),std::sin(a)},{.5f,std::cos(a),std::sin(a)},
                                 {.5f,std::cos(b),std::sin(b)},{-.5f,std::cos(b),std::sin(b)}};
            const scrp_vec3 n{0,std::cos((a+b)*.5f),std::sin((a+b)*.5f)};
            for (int index:{0,1,2,0,2,3}) v.push_back({p[index],n,{1,1,1,1}});
            for (int index:{0,3}) v.push_back({p[index],{-1,0,0},{1,1,1,1}});
            v.push_back({{-.5f,0,0},{-1,0,0},{1,1,1,1}});
            for (int index:{2,1}) v.push_back({p[index],{1,0,0},{1,1,1,1}});
            v.push_back({{.5f,0,0},{1,0,0},{1,1,1,1}});
        }
        return scrp_view3d_mesh_create(view,v.data(),static_cast<uint32_t>(v.size()),out,error);
    });
}
int scrp_view3d_begin(scrp_view3d *view,const scrp_camera *c,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!view || !c || view->frame || !scrp::capi::finite(c->eye) || !scrp::capi::finite(c->target) || !scrp::capi::finite(c->up) ||
            !(c->vertical_fov>0.1f && c->vertical_fov<3) || !(c->near_plane>0 && c->far_plane>c->near_plane) ||
            !(c->fog_end>c->fog_start) || !std::isfinite(c->far_plane) || !std::isfinite(c->fog_end) || !scrp::color_ok(c->fog_color) ||
            scrp_vec3_dot(scrp_vec3_cross(scrp_vec3_sub(c->target,c->eye),c->up),scrp_vec3_cross(scrp_vec3_sub(c->target,c->eye),c->up))<1e-6f)
            return scrp::capi::fail(error,"Begin requires a valid perspective camera and no active frame");
        for (const auto& l:c->lights) if (!scrp::capi::finite(l.position) || !scrp::capi::finite(l.direction) || !scrp::color_ok(l.color) || !std::isfinite(l.range) || !std::isfinite(l.cone_cos)) return scrp::capi::fail(error,"Lights must be finite");
        view->current(); SDL_GL_GetDrawableSize(view->window,&view->width,&view->height);
        glViewport(0,0,view->width,view->height);
        glClearColor(c->fog_color.r,c->fog_color.g,c->fog_color.b,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        auto& gl=view->gl; gl.UseProgram(view->program);
        view->matrix("vp",scrp::camera_matrix(*c,float(std::max(1,view->width))/std::max(1,view->height)));
        gl.Uniform1i(view->uniform("overlay"),0); view->color("fog_color",c->fog_color);
        gl.Uniform3f(view->uniform("eye"),c->eye.x,c->eye.y,c->eye.z);
        gl.Uniform1f(view->uniform("fog_start"),c->fog_start); gl.Uniform1f(view->uniform("fog_end"),c->fog_end);
        for (int i=0;i<2;++i) {
            const auto& l=c->lights[i]; const std::string index="["+std::to_string(i)+"]";
            gl.Uniform4f(view->uniform(("light_position"+index).c_str()),l.position.x,l.position.y,l.position.z,l.range);
            gl.Uniform4f(view->uniform(("light_direction"+index).c_str()),l.direction.x,l.direction.y,l.direction.z,l.cone_cos);
            view->color(("light_color"+index).c_str(),l.color);
        }
        view->frame=true; view->overlay=false; return 1;
    });
}
static int draw_mesh(scrp_view3d *view,scrp_mesh_id id,scrp_texture_id texture,scrp_pose pose,scrp_vec3 scale,scrp_color tint,float emissive,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!view || !view->frame || view->overlay || !view->meshes.count(id) || !scrp::capi::valid(pose) || !scrp::capi::finite(scale) ||
            scale.x<=0 || scale.y<=0 || scale.z<=0 || !scrp::color_ok(tint) || !std::isfinite(emissive)) return scrp::capi::fail(error,"Draw requires an active scene, owned mesh and valid transform");
        if (texture && (!view->textures.count(texture) || !view->meshes.at(id).has_uv))
            return scrp::capi::fail(error,"Textured draw requires an owned UV mesh and texture");
        view->current(); view->matrix("model",scrp::model(pose,scale)); view->color("tint",tint);
        view->gl.Uniform1i(view->uniform("textured"),texture!=0);
        if (texture) glBindTexture(GL_TEXTURE_2D,view->textures.at(texture));
        view->gl.Uniform1f(view->uniform("emissive"),std::clamp(emissive,0.0f,1.0f));
        const auto& mesh=view->meshes.at(id); view->gl.BindVertexArray(mesh.vao); glDrawArrays(GL_TRIANGLES,0,mesh.count);
        return 1;
    });
}
int scrp_view3d_draw(scrp_view3d *view,scrp_mesh_id id,scrp_pose pose,scrp_vec3 scale,scrp_color tint,float emissive,scrp_error *error) {
    return draw_mesh(view,id,0,pose,scale,tint,emissive,error);
}
int scrp_view3d_draw_textured(scrp_view3d *view,scrp_mesh_id id,scrp_texture_id texture,scrp_pose pose,scrp_vec3 scale,scrp_color tint,float emissive,scrp_error *error) {
    if (!texture) return scrp::capi::fail(error,"Textured draw requires a texture");
    return draw_mesh(view,id,texture,pose,scale,tint,emissive,error);
}
int scrp_view3d_overlay(scrp_view3d *view,const scrp_vertex *v,uint32_t count,float w,float h,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!view || !view->frame || !v || !count || count%3 || count>3000000 || !std::isfinite(w) || !std::isfinite(h) || w<=0 || h<=0)
            return scrp::capi::fail(error,"Overlay requires an active frame, triangles and a positive canvas");
        for (uint32_t i=0;i<count;++i) if (!scrp::capi::finite(v[i].position) || !scrp::color_ok(v[i].color)) return scrp::capi::fail(error,"Overlay vertices must be finite");
        view->current(); view->overlay=true; glDisable(GL_DEPTH_TEST);
        auto projection=scrp::identity(); projection[0]=2/w; projection[5]=-2/h; projection[12]=-1; projection[13]=1;
        view->matrix("vp",projection); view->matrix("model",scrp::identity()); view->color("tint",{1,1,1,1});
        view->gl.Uniform1i(view->uniform("overlay"),1);
        view->upload(view->stream,v,count,GL_STREAM_DRAW); glDrawArrays(GL_TRIANGLES,0,view->stream.count); return 1;
    });
}
int scrp_view3d_text(scrp_view3d *view,float x,float y,const char *text,float size,scrp_color color,float w,float h,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!view || !view->frame || !text || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(size) || size<=0 || std::strlen(text)>4096)
            return scrp::capi::fail(error,"Text requires an active frame, finite position/size and at most 4096 bytes");
        std::vector<scrp_vertex> vertices;
        const auto rect=[&](float px,float py) {
            const scrp_vec3 p[6]={{px,py,0},{px+size,py,0},{px+size,py+size,0},{px,py,0},{px+size,py+size,0},{px,py+size,0}};
            for (auto position:p) vertices.push_back({position,{0,0,1},color});
        };
        const float left=x;
        for (const char *p=text;*p;++p) {
            if (*p=='\n') { x=left; y+=size*7; continue; }
            const auto rows=scrp::MicroFont::glyphRows(*p);
            for (int row=0;row<5;++row) for (int col=0;col<3;++col)
                if (rows[row]&(1<<(2-col))) rect(x+col*size,y+row*size);
            x+=4*size;
        }
        return vertices.empty() ? 1 : scrp_view3d_overlay(view,vertices.data(),static_cast<uint32_t>(vertices.size()),w,h,error);
    });
}
int scrp_view3d_present(scrp_view3d *view,const char *capture,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!view || !view->frame) return scrp::capi::fail(error,"Present requires an active frame");
        view->current();
        if (capture && *capture && view->width>0 && view->height>0) {
            std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,view->width,view->height,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);
            if (!surface) return scrp::capi::fail(error,SDL_GetError());
            glReadPixels(0,0,view->width,view->height,GL_RGBA,GL_UNSIGNED_BYTE,surface->pixels);
            std::vector<unsigned char> row(surface->pitch);
            auto *pixels=static_cast<unsigned char*>(surface->pixels);
            for (int y=0;y<view->height/2;++y) {
                auto *a=pixels+y*surface->pitch; auto *b=pixels+(view->height-1-y)*surface->pitch;
                std::memcpy(row.data(),a,row.size()); std::memcpy(a,b,row.size()); std::memcpy(b,row.data(),row.size());
            }
            if (SDL_SaveBMP(surface.get(),capture)!=0) return scrp::capi::fail(error,SDL_GetError());
        }
        if (glGetError()!=GL_NO_ERROR) return scrp::capi::fail(error,"OpenGL frame failed");
        SDL_GL_SwapWindow(view->window); view->frame=false; return 1;
    });
}
const char *scrp_view3d_driver(const scrp_view3d *view) { return view ? view->driver.c_str() : ""; }
}
