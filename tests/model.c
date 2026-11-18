#include <scrp/c/Model.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr,"GLB check failed at line %d: %s\n",__LINE__,#test); return 1; } } while (0)

/* Interleaved triangle, reverse-wound indexed primitive and a second flat
   primitive sharing its positions. Keep the fixture independent of assets. */
static const char fixture[] =
    "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":160}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteLength\":144,\"byteStride\":48},"
    "{\"buffer\":0,\"byteOffset\":144,\"byteLength\":6},{\"buffer\":0,\"byteOffset\":152,\"byteLength\":8}],"
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"type\":\"VEC3\",\"count\":3},"
    "{\"bufferView\":0,\"byteOffset\":12,\"componentType\":5126,\"type\":\"VEC3\",\"count\":3},"
    "{\"bufferView\":0,\"byteOffset\":24,\"componentType\":5126,\"type\":\"VEC4\",\"count\":3},"
    "{\"bufferView\":0,\"byteOffset\":40,\"componentType\":5126,\"type\":\"VEC2\",\"count\":3},"
    "{\"bufferView\":1,\"componentType\":5123,\"type\":\"SCALAR\",\"count\":3}],"
    "\"images\":[{\"mimeType\":\"image/png\",\"bufferView\":2}],\"textures\":[{\"source\":0}],"
    "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],"
    "\"meshes\":[{\"name\":\"Example\",\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
    "\"COLOR_0\":2,\"TEXCOORD_0\":3},\"indices\":4,\"material\":0},{\"attributes\":{\"POSITION\":0}}]}]}";

static void word(uint8_t *p,uint32_t n) {
    p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); p[2]=(uint8_t)(n>>16); p[3]=(uint8_t)(n>>24);
}
static void component(uint8_t *p,float n) {
    uint32_t bits; memcpy(&bits,&n,4); word(p,bits);
}
static uint32_t make_file(uint8_t *data,const char *json,uint8_t **payload) {
    const uint32_t n=(uint32_t)strlen(json), padded=(n+3)&~3u, size=28+padded+160;
    memset(data,0,size);
    word(data,0x46546c67); word(data+4,2); word(data+8,size);
    word(data+12,padded); word(data+16,0x4e4f534a);
    memset(data+20,' ',padded); memcpy(data+20,json,n);
    word(data+20+padded,160); word(data+24+padded,0x004e4942);
    uint8_t *bin=data+28+padded;
    *payload=bin;
    for (unsigned i=0;i<3;++i) {
        uint8_t *v=bin+i*48;
        component(v,i==1); component(v+4,i==2); component(v+20,1);
        component(v+24,i*.25f); component(v+36,1); component(v+40,i*.5f); component(v+44,i*.25f);
    }
    bin[144]=2; bin[146]=1; bin[148]=0;
    memcpy(bin+152,"\x89PNG\r\n\x1a\n",8);
    return size;
}
static int rejected(const uint8_t *data,uint32_t size) {
    scrp_model *model=NULL;
    scrp_error error={{0}};
    const int ok=scrp_model_decode_glb(data,size,&model,&error);
    const int failed=!ok && !model && error.message[0];
    scrp_model_destroy(&model);
    return failed;
}

static int packed_case(int color16) {
    char json[sizeof(fixture)+64];
    const char *c=strstr(fixture,"\"byteOffset\":24,\"componentType\":5126")+strlen("\"byteOffset\":24,\"componentType\":");
    const char *t=strstr(c,"\"byteOffset\":40,\"componentType\":5126")+strlen("\"byteOffset\":40,\"componentType\":");
    snprintf(json,sizeof(json),"%.*s%d,\"normalized\":true%.*s%d,\"normalized\":true%s",
             (int)(c-fixture),fixture,color16?5123:5121,(int)(t-c-4),c+4,color16?5121:5123,t+4);
    if (!color16) memcpy(strstr(json,"VEC4"),"VEC3",4);
    uint8_t data[2048],*bin;
    const uint32_t size=make_file(data,json,&bin);
    for (unsigned i=0;i<3;++i) {
        uint8_t *c0=bin+i*48+24,*uv=bin+i*48+40;
        memset(c0,0,16); memset(uv,0,8);
        if (color16) { c0[0]=0xff; c0[1]=0x40; c0[6]=c0[7]=0xff; uv[0]=0x40; uv[1]=0xff; }
        else { c0[0]=0x40; uv[0]=0xff; uv[1]=0x40; uv[2]=uv[3]=0xff; }
    }
    scrp_model *m=NULL;
    if (!scrp_model_decode_glb(data,size,&m,NULL)) return 0;
    const scrp_model_primitive *p=scrp_model_get_primitive(m,0);
    const float small=64.f/255.f,wide=0x40ff/65535.f;
    const int ok=p->vertices[0].color.r==(color16?wide:small) && p->vertices[0].color.a==1 &&
                 p->uv_vertices[0].u==(color16?small:wide) && p->uv_vertices[0].v==1;
    scrp_model_destroy(&m);
    return ok;
}

int main(void) {
    uint8_t data[2048],*bin;
    uint32_t size=make_file(data,fixture,&bin);
    scrp_model *m=NULL;
    scrp_error error={{0}};
    CHECK(packed_case(0) && packed_case(1));
    CHECK(scrp_model_decode_glb(data,size,&m,&error));
    CHECK(scrp_model_primitive_count(m)==2 && scrp_model_image_count(m)==1);
    const scrp_model_primitive *a=scrp_model_find_primitive(m,"Example",0),*b=scrp_model_get_primitive(m,1);
    CHECK(a && b && b==scrp_model_find_primitive(m,"Example",1) && !scrp_model_find_primitive(m,"Missing",0));
    CHECK(a->vertex_count==3 && b->vertex_count==3 && !b->uv_vertices && !scrp_model_get_primitive(m,2));
    CHECK(a->vertices[0].position.y==1 && a->vertices[1].position.x==1 && a->vertices[2].position.x==0);
    CHECK(a->vertices[0].color.r==.5f && a->uv_vertices[0].u==1 && a->uv_vertices[0].v==.5f);
    CHECK(a->uv_vertices[0].vertex.normal.z==1 && b->vertices[1].normal.z==1 && b->vertices[2].color.g==1);
    CHECK(a->material_index==0 && a->base_color_image_index==0 && b->material_index==-1 && b->base_color_image_index==-1);
    size_t bytes=0;
    const char *mime=NULL;
    const uint8_t *image=scrp_model_image_data(m,0,&bytes,&mime);
    CHECK(image && bytes==8 && !strcmp(mime,"image/png") && !memcmp(image,bin+152,8));
    bin[152]=0;
    CHECK(image[0]==0x89 && !scrp_model_image_data(m,1,&bytes,&mime) && !bytes && !mime);
    CHECK(!scrp_model_decode_glb(data,size,&m,&error) && scrp_model_primitive_count(m)==2);
    scrp_model_destroy(&m);
    scrp_model_destroy(&m);
    CHECK(!m && !scrp_model_primitive_count(NULL) && !scrp_model_image_count(NULL));

    size=make_file(data,fixture,&bin);
    const char *path="scrp-model-contract.glb";
    FILE *file=fopen(path,"wb"); CHECK(file);
    CHECK(fwrite(data,1,size,file)==size && fclose(file)==0);
    const int loaded=scrp_model_load_glb(path,&m,&error);
    CHECK(remove(path)==0 && loaded && scrp_model_primitive_count(m)==2);
    scrp_model_destroy(&m);

    /* All edits retain their width so the enclosing GLB lengths stay valid. */
    struct edit { const char *from,*to; };
    const struct edit cases[]={
        {"\"byteStride\":48","\"byteStride\":47"}, {"\"byteLength\":144","\"byteLength\":120"},
        {"\"byteOffset\":12","\"byteOffset\":11"}, {"\"byteOffset\":152","\"byteOffset\":155"},
        {"\"byteLength\":160","\"byteLength\":159"}, {"\"componentType\":5126","\"componentType\":5123"},
        {"\"type\":\"VEC2\"","\"type\":\"VEC4\""}, {"\"count\":3","\"count\":4"},
        {"\"source\":0","\"source\":9"}, {"\"bufferView\":2","\"bufferView\":8"},
        {"\"material\":0","\"material\":9"}, {"\"index\":0","\"index\":9"},
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        char json[sizeof(fixture)]; memcpy(json,fixture,sizeof(json));
        char *s=strstr(json,cases[i].from); CHECK(s && strlen(cases[i].from)==strlen(cases[i].to));
        memcpy(s,cases[i].to,strlen(cases[i].to));
        size=make_file(data,json,&bin); CHECK(rejected(data,size));
    }
    size=make_file(data,fixture,&bin);
    bin[144]=3; CHECK(rejected(data,size));
    bin[144]=2; word(bin,0x7fc00000); CHECK(rejected(data,size));
    size=make_file(data,fixture,&bin); CHECK(rejected(data,size-1));
    word(data+12,UINT32_MAX-3); CHECK(rejected(data,size));
    return 0;
}
