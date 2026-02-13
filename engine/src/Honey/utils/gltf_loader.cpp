#include "hnpch.h"
#include "gltf_loader.h"

#include "Honey/core/log.h"
#include "Honey/renderer/buffer.h"
#include "Honey/renderer/texture.h"
#include "Honey/renderer/vertex_array.h"

#include <glm/glm.hpp>

#define TINYGLTF_IMPLEMENTATION
// stb_image is compiled elsewhere (existing texture stuff)
//#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/quaternion.hpp"

namespace Honey {

    namespace {

        // Local vertex type matching your current 3D shader expectations.
        // If you already have TestVertex3D exposed in a header, prefer including that instead.
        struct VertexPNUV {
            glm::vec3 position{0.0f};
            glm::vec3 normal{0.0f, 0.0f, 1.0f};
            glm::vec2 uv{0.0f};
        };

        static bool has_ext(const std::filesystem::path& p, const char* ext) {
            auto e = p.extension().string();
            for (auto& c : e) c = (char)std::tolower(c);
            return e == ext;
        }

        static const tinygltf::Accessor* find_accessor(const tinygltf::Model& model, int accessorIndex) {
            if (accessorIndex < 0 || accessorIndex >= (int)model.accessors.size())
                return nullptr;
            return &model.accessors[(size_t)accessorIndex];
        }

        static const tinygltf::BufferView* find_buffer_view(const tinygltf::Model& model, int viewIndex) {
            if (viewIndex < 0 || viewIndex >= (int)model.bufferViews.size())
                return nullptr;
            return &model.bufferViews[(size_t)viewIndex];
        }

        static const tinygltf::Buffer* find_buffer(const tinygltf::Model& model, int bufferIndex) {
            if (bufferIndex < 0 || bufferIndex >= (int)model.buffers.size())
                return nullptr;
            return &model.buffers[(size_t)bufferIndex];
        }

        static size_t component_type_size(int componentType) {
            switch (componentType) {
                case TINYGLTF_COMPONENT_TYPE_BYTE:           return 1;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  return 1;
                case TINYGLTF_COMPONENT_TYPE_SHORT:          return 2;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
                case TINYGLTF_COMPONENT_TYPE_INT:            return 4;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   return 4;
                case TINYGLTF_COMPONENT_TYPE_FLOAT:          return 4;
                case TINYGLTF_COMPONENT_TYPE_DOUBLE:         return 8;
                default: return 0;
            }
        }

        static int type_num_components(int type) {
            switch (type) {
                case TINYGLTF_TYPE_SCALAR: return 1;
                case TINYGLTF_TYPE_VEC2:   return 2;
                case TINYGLTF_TYPE_VEC3:   return 3;
                case TINYGLTF_TYPE_VEC4:   return 4;
                case TINYGLTF_TYPE_MAT2:   return 4;
                case TINYGLTF_TYPE_MAT3:   return 9;
                case TINYGLTF_TYPE_MAT4:   return 16;
                default: return 0;
            }
        }

        static const uint8_t* accessor_data_ptr(const tinygltf::Model& model, const tinygltf::Accessor& acc, size_t& outStride) {
            const tinygltf::BufferView* bv = find_buffer_view(model, acc.bufferView);
            if (!bv) return nullptr;

            const tinygltf::Buffer* buf = find_buffer(model, bv->buffer);
            if (!buf) return nullptr;

            const size_t compSize = component_type_size(acc.componentType);
            const int comps = type_num_components(acc.type);
            if (compSize == 0 || comps == 0) return nullptr;

            const size_t elemSize = compSize * (size_t)comps;
            outStride = (bv->byteStride != 0) ? (size_t)bv->byteStride : elemSize;

            const size_t start = (size_t)bv->byteOffset + (size_t)acc.byteOffset;
            if (start >= buf->data.size()) return nullptr;

            return buf->data.data() + start;
        }

        static bool read_vec3_float(const tinygltf::Model& model, const tinygltf::Accessor& acc, size_t index, glm::vec3& out) {
            if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || acc.type != TINYGLTF_TYPE_VEC3)
                return false;

            size_t stride = 0;
            const uint8_t* base = accessor_data_ptr(model, acc, stride);
            if (!base) return false;

            const float* f = reinterpret_cast<const float*>(base + index * stride);
            out = glm::vec3(f[0], f[1], f[2]);
            return true;
        }

        static bool read_vec2_float(const tinygltf::Model& model, const tinygltf::Accessor& acc, size_t index, glm::vec2& out) {
            if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || acc.type != TINYGLTF_TYPE_VEC2)
                return false;

            size_t stride = 0;
            const uint8_t* base = accessor_data_ptr(model, acc, stride);
            if (!base) return false;

            const float* f = reinterpret_cast<const float*>(base + index * stride);
            out = glm::vec2(f[0], f[1]);
            return true;
        }

        static Ref<Material> build_material_from_gltf(
            const tinygltf::Model& model,
            int materialIndex,
            const std::filesystem::path& gltfDir,
            const GltfLoadOptions& options,
            std::unordered_map<int, Ref<Texture2D>>& textureCacheByImageIndex
        ) {
            Ref<Material> mat = Material::create();

            if (materialIndex < 0 || materialIndex >= (int)model.materials.size()) {
                mat->set_base_color_factor(glm::vec4(1.0f));
                mat->set_base_color_texture(nullptr);
                return mat;
            }

            const tinygltf::Material& gm = model.materials[(size_t)materialIndex];

            // baseColorFactor
            glm::vec4 factor(1.0f);
            if (gm.pbrMetallicRoughness.baseColorFactor.size() == 4) {
                factor.r = (float)gm.pbrMetallicRoughness.baseColorFactor[0];
                factor.g = (float)gm.pbrMetallicRoughness.baseColorFactor[1];
                factor.b = (float)gm.pbrMetallicRoughness.baseColorFactor[2];
                factor.a = (float)gm.pbrMetallicRoughness.baseColorFactor[3];
            }
            mat->set_base_color_factor(factor);

            if (options.disable_textures) {
                mat->set_base_color_texture(nullptr);
                return mat;
            }

            // baseColorTexture (URI-based only for v1)
            const tinygltf::TextureInfo& ti = gm.pbrMetallicRoughness.baseColorTexture;
            if (ti.index < 0 || ti.index >= (int)model.textures.size()) {
                mat->set_base_color_texture(nullptr);
                return mat;
            }

            const tinygltf::Texture& gt = model.textures[(size_t)ti.index];
            if (gt.source < 0 || gt.source >= (int)model.images.size()) {
                mat->set_base_color_texture(nullptr);
                return mat;
            }

            auto it = textureCacheByImageIndex.find(gt.source);
            if (it != textureCacheByImageIndex.end()) {
                mat->set_base_color_texture(it->second);
                return mat;
            }

            const tinygltf::Image& img = model.images[(size_t)gt.source];

            // For milestone 1: only support images with URI on disk.
            if (img.uri.empty()) {
                HN_CORE_WARN("glTF: image source has no uri (embedded images not supported yet). Using white.");
                mat->set_base_color_texture(nullptr);
                return mat;
            }

            const std::filesystem::path texPath = gltfDir / img.uri;
            Ref<Texture2D> tex = Texture2D::create(texPath);
            textureCacheByImageIndex[gt.source] = tex;
            mat->set_base_color_texture(tex);

            return mat;
        }

        static void set_default_layout_pnuv(const Ref<VertexBuffer>& vb) {
            vb->set_layout({
                { ShaderDataType::Float3, "a_position" },
                { ShaderDataType::Float3, "a_normal"   },
                { ShaderDataType::Float2, "a_uv"       },
            });
        }

        static glm::mat4 gltf_mat4_to_glm(const std::vector<double>& m) {
            // Robust conversion: copy to float[16] then use glm::make_mat4 (column-major).
            if (m.size() != 16)
                return glm::mat4(1.0f);

            float f[16]{};
            for (int i = 0; i < 16; ++i)
                f[i] = (float)m[(size_t)i];

            return glm::make_mat4(f);
        }

        static glm::mat4 node_local_transform(const tinygltf::Node& n) {
            if (n.matrix.size() == 16) {
                return gltf_mat4_to_glm(n.matrix);
            }

            glm::vec3 t(0.0f);
            if (n.translation.size() == 3) {
                t = glm::vec3((float)n.translation[0], (float)n.translation[1], (float)n.translation[2]);
            }

            glm::quat r(1.0f, 0.0f, 0.0f, 0.0f); // (w,x,y,z)
            if (n.rotation.size() == 4) {
                r = glm::quat((float)n.rotation[3], (float)n.rotation[0], (float)n.rotation[1], (float)n.rotation[2]);
            }

            glm::vec3 s(1.0f);
            if (n.scale.size() == 3) {
                s = glm::vec3((float)n.scale[0], (float)n.scale[1], (float)n.scale[2]);
            }

            return glm::translate(glm::mat4(1.0f), t) * glm::toMat4(r) * glm::scale(glm::mat4(1.0f), s);
        }

        static glm::vec3 extract_translation(const glm::mat4& m) {
            // Column-major: translation is column 3 xyz (m[3].xyz)
            return glm::vec3(m[3][0], m[3][1], m[3][2]);
        }

        // Extract all primitives for a specific glTF mesh index and append to `out`,
        // tagging each Submesh with the provided world transform.
        static void append_mesh_primitives_as_submeshes(
            Ref<Mesh>& out,
            const tinygltf::Model& model,
            int gltfMeshIndex,
            const glm::mat4& worldTransform,
            const std::filesystem::path& gltfDir,
            const GltfLoadOptions& options,
            std::unordered_map<int, Ref<Texture2D>>& textureCacheByImageIndex
        ) {
            if (gltfMeshIndex < 0 || gltfMeshIndex >= (int)model.meshes.size())
                return;

            const tinygltf::Mesh& gm = model.meshes[(size_t)gltfMeshIndex];

            for (size_t primIndex = 0; primIndex < gm.primitives.size(); ++primIndex) {
                const tinygltf::Primitive& prim = gm.primitives[primIndex];

                if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
                    HN_CORE_WARN("glTF: skipping primitive (mode != TRIANGLES)");
                    continue;
                }

                // POSITION is required
                auto posIt = prim.attributes.find("POSITION");
                if (posIt == prim.attributes.end()) {
                    HN_CORE_WARN("glTF: primitive has no POSITION, skipping");
                    continue;
                }

                const tinygltf::Accessor* posAcc = find_accessor(model, posIt->second);
                if (!posAcc || posAcc->count == 0) {
                    HN_CORE_WARN("glTF: invalid POSITION accessor, skipping");
                    continue;
                }

                const size_t vcount = (size_t)posAcc->count;

                const tinygltf::Accessor* nrmAcc = nullptr;
                if (auto it = prim.attributes.find("NORMAL"); it != prim.attributes.end())
                    nrmAcc = find_accessor(model, it->second);

                const tinygltf::Accessor* uvAcc = nullptr;
                if (auto it = prim.attributes.find("TEXCOORD_0"); it != prim.attributes.end())
                    uvAcc = find_accessor(model, it->second);

                if (!nrmAcc && !options.allow_missing_attributes) {
                    HN_CORE_WARN("glTF: primitive missing NORMAL and allow_missing_attributes=false, skipping");
                    continue;
                }
                if (!uvAcc && !options.allow_missing_attributes) {
                    HN_CORE_WARN("glTF: primitive missing TEXCOORD_0 and allow_missing_attributes=false, skipping");
                    continue;
                }

                std::vector<VertexPNUV> vertices;
                vertices.resize(vcount);

                for (size_t i = 0; i < vcount; ++i) {
                    glm::vec3 p3{};
                    if (!read_vec3_float(model, *posAcc, i, p3)) {
                        HN_CORE_WARN("glTF: POSITION read failed, skipping primitive");
                        vertices.clear();
                        break;
                    }
                    vertices[i].position = p3;

                    if (nrmAcc) {
                        glm::vec3 n3{};
                        if (read_vec3_float(model, *nrmAcc, i, n3))
                            vertices[i].normal = n3;
                    }

                    if (uvAcc) {
                        glm::vec2 t2{};
                        if (read_vec2_float(model, *uvAcc, i, t2))
                            vertices[i].uv = t2;
                    }
                }

                if (vertices.empty())
                    continue;

                // Indices (required for now)
                if (prim.indices < 0) {
                    HN_CORE_WARN("glTF: primitive has no indices (non-indexed not supported yet), skipping");
                    continue;
                }

                const tinygltf::Accessor* idxAcc = find_accessor(model, prim.indices);
                if (!idxAcc || idxAcc->count == 0) {
                    HN_CORE_WARN("glTF: invalid indices accessor, skipping");
                    continue;
                }

                size_t idxStride = 0;
                const uint8_t* idxBase = accessor_data_ptr(model, *idxAcc, idxStride);
                if (!idxBase) {
                    HN_CORE_WARN("glTF: indices base pointer invalid, skipping");
                    continue;
                }

                Ref<VertexArray> vao = VertexArray::create();

                // Vertex buffer
                Ref<VertexBuffer> vb = VertexBuffer::create((uint32_t)(vertices.size() * sizeof(VertexPNUV)));
                vb->set_data(vertices.data(), (uint32_t)(vertices.size() * sizeof(VertexPNUV)));
                set_default_layout_pnuv(vb);
                vao->add_vertex_buffer(vb);

                // Index buffer (u16 or u32)
                Ref<IndexBuffer> ib;
                if (idxAcc->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    std::vector<uint16_t> indices(idxAcc->count);
                    for (size_t i = 0; i < indices.size(); ++i) {
                        const uint16_t* v = reinterpret_cast<const uint16_t*>(idxBase + i * idxStride);
                        indices[i] = *v;
                    }
                    ib = IndexBuffer::create(indices.data(), (uint32_t)indices.size());
                } else if (idxAcc->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                    std::vector<uint32_t> indices(idxAcc->count);
                    for (size_t i = 0; i < indices.size(); ++i) {
                        const uint32_t* v = reinterpret_cast<const uint32_t*>(idxBase + i * idxStride);
                        indices[i] = *v;
                    }
                    ib = IndexBuffer::create(indices.data(), (uint32_t)indices.size());
                } else {
                    HN_CORE_WARN("glTF: indices componentType not supported (need UNSIGNED_SHORT/UNSIGNED_INT), skipping");
                    continue;
                }

                vao->set_index_buffer(ib);

                // Material
                Ref<Material> mat = build_material_from_gltf(
                    model,
                    prim.material,
                    gltfDir,
                    options,
                    textureCacheByImageIndex
                );

                Submesh sm{};
                sm.vao = vao;
                sm.material = mat;
                sm.name = gm.name.empty() ? ("Mesh_" + std::to_string(gltfMeshIndex) + "_Prim_" + std::to_string(primIndex)) : gm.name;
                sm.transform = worldTransform;

                out->add_submesh(std::move(sm));
            }
        }

        static void traverse_node_tree_and_emit_meshes(
            Ref<Mesh>& out,
            const tinygltf::Model& model,
            int nodeIndex,
            const glm::mat4& parentWorld,
            const std::filesystem::path& gltfDir,
            const GltfLoadOptions& options,
            std::unordered_map<int, Ref<Texture2D>>& textureCacheByImageIndex
        ) {
            if (nodeIndex < 0 || nodeIndex >= (int)model.nodes.size())
                return;

            const tinygltf::Node& node = model.nodes[(size_t)nodeIndex];

            const glm::mat4 local = node_local_transform(node);
            const glm::mat4 world = parentWorld * local;

            if (node.mesh >= 0) {
                append_mesh_primitives_as_submeshes(
                    out,
                    model,
                    node.mesh,
                    world,
                    gltfDir,
                    options,
                    textureCacheByImageIndex
                );
            }

            for (int child : node.children) {
                traverse_node_tree_and_emit_meshes(
                    out,
                    model,
                    child,
                    world,
                    gltfDir,
                    options,
                    textureCacheByImageIndex
                );
            }
        }

    } // namespace

Ref<Mesh> load_gltf_mesh(const std::filesystem::path& path, const GltfLoadOptions& options) {
    if (!std::filesystem::exists(path)) {
        HN_CORE_ERROR("load_gltf_mesh: file does not exist: {}", path.string());
        return nullptr;
    }

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;

    std::string err;
    std::string warn;

    bool ok = false;
    const std::string p = path.string();
    if (has_ext(path, ".glb")) {
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, p);
    } else {
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, p);
    }

    if (!warn.empty())
        HN_CORE_WARN("glTF warn: {}", warn);
    if (!err.empty())
        HN_CORE_ERROR("glTF err: {}", err);
    if (!ok) {
        HN_CORE_ERROR("Failed to load glTF: {}", path.string());
        return nullptr;
    }

    Ref<Mesh> out = Mesh::create(path.filename().string());

    const std::filesystem::path gltfDir = path.parent_path();
    std::unordered_map<int, Ref<Texture2D>> textureCacheByImageIndex;

    // Traverse the scene graph so node transforms are applied.
    // IMPORTANT: do NOT also do a second pass over model.meshes (that would duplicate submeshes at identity).
    int sceneIndex = model.defaultScene;
    if (sceneIndex < 0 || sceneIndex >= (int)model.scenes.size()) {
        sceneIndex = model.scenes.empty() ? -1 : 0;
    }

    if (sceneIndex >= 0) {
        const tinygltf::Scene& scene = model.scenes[(size_t)sceneIndex];
        const glm::mat4 I(1.0f);

        for (int rootNode : scene.nodes) {
            traverse_node_tree_and_emit_meshes(
                out,
                model,
                rootNode,
                I,
                gltfDir,
                options,
                textureCacheByImageIndex
            );
        }
    } else {
        // No scenes? Fallback: emit all meshes at identity.
        HN_CORE_WARN("glTF: model has no scenes; emitting meshes with identity transforms.");
        const glm::mat4 I(1.0f);
        for (int mi = 0; mi < (int)model.meshes.size(); ++mi) {
            append_mesh_primitives_as_submeshes(
                out,
                model,
                mi,
                I,
                gltfDir,
                options,
                textureCacheByImageIndex
            );
        }
    }

    if (out->empty()) {
        HN_CORE_WARN("load_gltf_mesh: loaded 0 primitives from {}", path.string());
    } else {
        HN_CORE_INFO("load_gltf_mesh: loaded {} submeshes from {}", out->submesh_count(), path.string());
    }

    return out;
}

} // namespace Honey