#include <scrp/c/Model.h>
#include <scrp/Json.h>
#include "CApiInternal.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace scrp {
namespace {
constexpr uint32_t max_file_bytes = 128u << 20;
constexpr uint32_t max_json_bytes = 8u << 20;
constexpr uint32_t max_vertices = 3000000; // Total expanded geometry in this resource.
constexpr uint32_t float32 = 5126;

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

uint32_t u32(const uint8_t *bytes) {
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
           (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}
uint32_t u16(const uint8_t *bytes) { return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8); }

uint32_t integer(const JsonValue& value, const char *field) {
    if (value.type != JsonValue::Type::Number || !(value.number >= 0 && value.number <= UINT32_MAX) ||
        std::floor(value.number) != value.number)
        throw std::runtime_error(std::string("Invalid unsigned GLB field: ") + field);
    return static_cast<uint32_t>(value.number);
}

uint32_t field(const JsonValue& object, const char *key, uint32_t fallback) {
    return object.contains(key) ? integer(object[key], key) : fallback;
}

struct Primitive {
    scrp_model_primitive view{};
    std::vector<scrp_vertex> vertices;
    std::vector<scrp_uv_vertex> textured;
};

struct Document {
    JsonValue root;
    const uint8_t *bin = nullptr;
    uint32_t bin_size = 0;
};

struct BufferView {
    uint32_t start, size;
};

BufferView bounds(const Document& doc, const JsonValue& view) {
    require(integer(view["buffer"],"bufferView.buffer") == 0,"GLB view references an external buffer");
    const uint32_t start = field(view,"byteOffset",0), size = integer(view["byteLength"],"bufferView.byteLength");
    require(size && start <= doc.bin_size && size <= doc.bin_size-start,"GLB buffer view exceeds its buffer");
    return {start,size};
}

struct Image {
    BufferView view;
    const char *mime;
};

Document read_chunks(const uint8_t *data, uint32_t size) {
    require(data && size >= 28 && size <= max_file_bytes, "Invalid GLB file size");
    require(u32(data) == 0x46546c67 && u32(data+4) == 2 && u32(data+8) == size,
            "Expected a complete glTF 2.0 binary file");
    Document doc;
    for (uint32_t offset=12, chunk=0; offset < size; ++chunk) {
        require(size-offset >= 8, "Truncated GLB chunk header");
        const uint32_t length = u32(data+offset), type = u32(data+offset+4);
        offset += 8;
        require(length <= size-offset && length%4 == 0, "Invalid GLB chunk length");
        if (chunk == 0) {
            require(type == 0x4e4f534a && length && length <= max_json_bytes, "Invalid GLB JSON chunk");
            std::string diagnostic;
            const std::string json(reinterpret_cast<const char *>(data+offset),length);
            if (!Json::parse(json,doc.root,&diagnostic)) throw std::runtime_error("GLB JSON: " + diagnostic);
        } else if (type == 0x004e4942) {
            require(chunk == 1 && !doc.bin, "GLB binary buffer must be the second chunk");
            doc.bin = data+offset;
            doc.bin_size = length;
        } else {
            require(type != 0x4e4f534a, "Duplicate GLB JSON chunk");
        }
        offset += length;
    }
    require(doc.bin && doc.root["asset"]["version"].asString() == "2.0", "Expected glTF 2.0 with an embedded buffer");
    const auto& required = doc.root["extensionsRequired"];
    require(!doc.root.contains("extensionsRequired") || (required.isArray() && required.array.empty()),
            "Unsupported required glTF extension");
    const auto& buffers = doc.root["buffers"];
    require(buffers.isArray() && buffers.size() == 1 && !buffers.at(0).contains("uri"), "Expected one embedded GLB buffer");
    const auto declared = integer(buffers.at(0)["byteLength"],"buffer.byteLength");
    require(declared && declared <= doc.bin_size && doc.bin_size-declared <= 3, "GLB buffer length disagrees with binary chunk");
    doc.bin_size = declared; // Accessors cannot address optional chunk padding.
    return doc;
}

struct Stream {
    const uint8_t *data = nullptr;
    uint32_t count = 0, stride = 0, component = 0, lanes = 0;

    float number(uint32_t i, uint32_t lane) const {
        const uint8_t *p=data+size_t(i)*stride;
        if (component == 5121) return p[lane]/255.f;
        if (component == 5123) return u16(p+2*lane)/65535.f;
        const uint32_t bits = u32(p+lane*4);
        static_assert(sizeof(float) == sizeof(bits) && std::numeric_limits<float>::is_iec559);
        float value;
        std::memcpy(&value,&bits,sizeof(value));
        require(std::isfinite(value),"Non-finite GLB vertex attribute");
        return value;
    }
    scrp_vec3 vec3(uint32_t i) const { return {number(i,0),number(i,1),number(i,2)}; }
    uint32_t index(uint32_t i) const {
        const uint8_t *p = data+size_t(i)*stride;
        if (component == 5121) return p[0];
        if (component == 5123) return u16(p);
        return u32(p);
    }
};

// Validate both the entire buffer view and the last complete strided element
// before exposing a pointer. Interleaved attributes can share one view.
Stream stream(const Document& doc, const JsonValue& source, const char *key, uint32_t lanes,
              bool optional=false, bool allow_packed=false) {
    if (optional && !source.contains(key)) return {};
    const auto& a = doc.root["accessors"].at(integer(source[key],key));
    require(a.isObject() && !a.contains("sparse"),"Missing accessor or unsupported sparse GLB accessor");
    require(!a.contains("normalized") || a["normalized"].type == JsonValue::Type::Bool,"Invalid GLB normalization flag");
    const auto& view = doc.root["bufferViews"].at(integer(a["bufferView"],"accessor.bufferView"));
    Stream s;
    s.count = integer(a["count"],"accessor.count");
    s.component = integer(a["componentType"],"accessor.componentType");
    const bool indices = lanes == 1;
    const bool small_unsigned = s.component == 5121 || s.component == 5123;
    const bool normalized = a["normalized"].boolean;
    if (indices) require(!normalized && (small_unsigned || s.component == 5125),"Invalid GLB index component type");
    else require((s.component == float32 && !normalized) || (allow_packed && small_unsigned && normalized),
                 "Unsupported GLB vertex component type or normalization");
    const auto& type = a["type"].str;
    s.lanes = (lanes == 4 && type == "VEC3") ? 3 : lanes;
    const char *shapes[] = {"","SCALAR","VEC2","VEC3","VEC4"};
    require(type == shapes[s.lanes],"Wrong GLB accessor shape");
    const uint32_t component_bytes = s.component == 5121 ? 1 : s.component == 5123 ? 2 : 4;
    const uint32_t element_bytes = component_bytes*s.lanes;
    const auto [view_start,view_bytes] = bounds(doc,view);
    const uint32_t start = field(a,"byteOffset",0);
    require(start <= view_bytes && start%component_bytes == 0 && (view_start+start)%component_bytes == 0,
            "Invalid GLB accessor offset");
    s.stride = field(view,"byteStride",element_bytes);
    if (view.contains("byteStride"))
        require(!indices && s.stride >= element_bytes && s.stride <= 252 && s.stride%4 == 0, "Invalid GLB vertex stride");
    require(s.count && s.count <= max_vertices && uint64_t(s.count-1)*s.stride+element_bytes <= view_bytes-start,
            "GLB accessor exceeds its buffer view or vertex limit");
    s.data = doc.bin+view_start+start;
    return s;
}

Primitive read_primitive(const Document& doc, const JsonValue& p, const char *name, uint32_t index, uint32_t& total) {
    require(p.isObject() && field(p,"mode",4) == 4,"Expected a TRIANGLES mesh primitive");
    const auto& attr = p["attributes"];
    const Stream positions=stream(doc,attr,"POSITION",3);
    const Stream normals=stream(doc,attr,"NORMAL",3,true);
    const Stream colors=stream(doc,attr,"COLOR_0",4,true,true);
    const Stream uv=stream(doc,attr,"TEXCOORD_0",2,true,true);
    const Stream indices=stream(doc,p,"indices",1,true);
    require((!normals.data || normals.count == positions.count) && (!colors.data || colors.count == positions.count) &&
            (!uv.data || uv.count == positions.count),"Mismatched GLB vertex attribute counts");
    const uint32_t count = indices.data ? indices.count : positions.count;
    require(count%3 == 0 && count <= max_vertices-total,"Invalid expanded GLB triangle count");
    total += count;
    Primitive part;
    part.view.mesh_name = name;
    part.view.primitive_index = index;
    part.view.material_index = -1;
    part.view.base_color_image_index = -1;
    if (p.contains("material")) {
        const uint32_t m = integer(p["material"],"primitive.material");
        const auto& material = doc.root["materials"].at(m);
        require(material.isObject() && m <= INT32_MAX,"Invalid GLB material index");
        part.view.material_index = static_cast<int32_t>(m);
        const auto& pbr = material["pbrMetallicRoughness"];
        if (pbr.contains("baseColorTexture")) {
            const auto& ref = pbr["baseColorTexture"];
            const uint32_t t = integer(ref["index"],"baseColorTexture.index");
            require(uv.data && field(ref,"texCoord",0) == 0,"Base color texture requires TEXCOORD_0");
            const uint32_t img = integer(doc.root["textures"].at(t)["source"],"texture.source");
            require(doc.root["images"].at(img).isObject() && img <= INT32_MAX,"Invalid GLB base color image");
            part.view.base_color_image_index = static_cast<int32_t>(img);
        }
    }
    part.vertices.resize(count);
    if (uv.data) part.textured.resize(count);
    for (uint32_t i=0; i<count; ++i) {
        const uint32_t j = indices.data ? indices.index(i) : i;
        require(j < positions.count,"GLB triangle index exceeds its vertex stream");
        auto& v = part.vertices[i];
        v.position = positions.vec3(j);
        if (normals.data) v.normal = normals.vec3(j);
        v.color = {1,1,1,1};
        if (colors.data)
            v.color = {colors.number(j,0),colors.number(j,1),colors.number(j,2),colors.lanes == 4 ? colors.number(j,3) : 1.f};
        if (uv.data) {
            part.textured[i].u = uv.number(j,0);
            part.textured[i].v = uv.number(j,1);
        }
    }
    if (!normals.data) {
        for (uint32_t i=0; i<count; i+=3) {
            auto *tri = part.vertices.data()+i;
            const auto normal = scrp_vec3_normalize(scrp_vec3_cross(scrp_vec3_sub(tri[1].position,tri[0].position),
                                                                    scrp_vec3_sub(tri[2].position,tri[0].position)));
            tri[0].normal = tri[1].normal = tri[2].normal = normal;
        }
    }
    if (uv.data)
        for (uint32_t i=0; i<count; ++i) part.textured[i].vertex = part.vertices[i];
    return part;
}
} // namespace
} // namespace scrp

struct scrp_model {
    std::vector<std::string> names;
    std::vector<scrp::Primitive> parts;
    std::vector<scrp::Image> images;
    std::vector<uint8_t> binary;
};

extern "C" int scrp_model_decode_glb(const uint8_t *data,uint32_t size,scrp_model **out,scrp_error *error) {
    using namespace scrp;
    return capi::boundary(error,[&] {
        require(out && !*out,"Expected an empty model handle");
        const Document doc = read_chunks(data,size);
        const auto& meshes = doc.root["meshes"];
        require(meshes.isArray() && !meshes.array.empty(),"GLB contains no meshes");
        auto model = std::make_unique<scrp_model>();
        const auto& images = doc.root["images"];
        require(!doc.root.contains("images") || images.isArray(),"Invalid GLB image array");
        for (const auto& image : images.array) {
            const auto& mime = image["mimeType"].str;
            require(!image.contains("uri") && (mime == "image/png" || mime == "image/jpeg"),"Expected an embedded GLB PNG or JPEG");
            const auto& view = doc.root["bufferViews"].at(integer(image["bufferView"],"image.bufferView"));
            require(!view.contains("byteStride"),"Embedded image cannot use an interleaved buffer view");
            model->images.push_back({bounds(doc,view), mime == "image/png" ? "image/png" : "image/jpeg"});
        }
        if (!model->images.empty()) model->binary.assign(doc.bin,doc.bin+doc.bin_size);
        model->names.reserve(meshes.size()); // The primitive views borrow these strings.
        uint32_t total=0;
        for (const auto& mesh : meshes.array) {
            const auto& list = mesh["primitives"];
            require(list.isArray() && !list.array.empty(),"GLB mesh contains no primitives");
            require(!mesh.contains("name") || mesh["name"].type == JsonValue::Type::String,"Invalid GLB mesh name");
            const auto& name = model->names.emplace_back(mesh["name"].asString());
            require(name.find('\0') == std::string::npos,"GLB mesh name contains a null character");
            for (size_t n=0; n<list.size(); ++n) {
                try {
                    model->parts.push_back(read_primitive(doc,list.at(n),name.c_str(),static_cast<uint32_t>(n),total));
                } catch (const std::runtime_error& e) {
                    throw std::runtime_error("GLB mesh " + name + ", primitive " + std::to_string(n) + ": " + e.what());
                }
            }
        }
        for (auto& part : model->parts) {
            part.view.vertices = part.vertices.data();
            part.view.vertex_count = static_cast<uint32_t>(part.vertices.size());
            part.view.uv_vertices = part.textured.empty() ? nullptr : part.textured.data();
        }
        *out = model.release();
        return 1;
    });
}

extern "C" int scrp_model_load_glb(const char *path,scrp_model **out,scrp_error *error) {
    using namespace scrp;
    return capi::boundary(error,[&] {
        require(path && *path && out && !*out,"Expected a GLB path and empty model handle");
        std::ifstream file(std::filesystem::u8path(path),std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error(std::string("Could not open GLB file: ") + path);
        const std::streamoff size = file.tellg();
        require(size >= 28 && size <= max_file_bytes,"Invalid GLB file size");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        file.seekg(0);
        require(bool(file.read(reinterpret_cast<char *>(bytes.data()),size)),"Incomplete GLB file read");
        return scrp_model_decode_glb(bytes.data(),static_cast<uint32_t>(size),out,error);
    });
}

extern "C" void scrp_model_destroy(scrp_model **model) {
    if (model) { delete *model; *model = nullptr; }
}
extern "C" uint32_t scrp_model_primitive_count(const scrp_model *model) {
    return model ? static_cast<uint32_t>(model->parts.size()) : 0;
}
extern "C" const scrp_model_primitive *scrp_model_get_primitive(const scrp_model *model,uint32_t index) {
    return model && index < model->parts.size() ? &model->parts[index].view : nullptr;
}
extern "C" const scrp_model_primitive *scrp_model_find_primitive(const scrp_model *model,const char *name,uint32_t index) {
    if (model && name)
        for (const auto& p : model->parts)
            if (p.view.primitive_index == index && std::strcmp(p.view.mesh_name,name) == 0) return &p.view;
    return nullptr;
}
extern "C" uint32_t scrp_model_image_count(const scrp_model *model) {
    return model ? static_cast<uint32_t>(model->images.size()) : 0;
}
extern "C" const uint8_t *scrp_model_image_data(const scrp_model *model,uint32_t index,size_t *byte_count,const char **mime_type) {
    if (byte_count) *byte_count = 0;
    if (mime_type) *mime_type = nullptr;
    if (!model || index >= model->images.size()) return nullptr;
    const auto& image = model->images[index];
    if (byte_count) *byte_count = image.view.size;
    if (mime_type) *mime_type = image.mime;
    return model->binary.data()+image.view.start;
}
