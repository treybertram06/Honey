#include "hnpch.h"
#include "gltf_loader.h"
#include "gltf_scene_tree.h"

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
#include "meshoptimizer.h"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/quaternion.hpp"
#include "Honey/core/task_system.h"

namespace Honey {

    namespace {
        // Local vertex type matching your current 3D shader expectations.
        // If you already have TestVertex3D exposed in a header, prefer including that instead.
        struct VertexPNUV {
            glm::vec3 position{0.0f};
            glm::vec3 normal{0.0f, 0.0f, 1.0f};
            glm::vec2 uv{0.0f};
        };

        struct ImportedPrimitiveData {
            std::string name;

            std::vector<VertexPNUV> vertices;
            std::vector<uint32_t> indices;

            Ref<Material> material;
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
            std::unordered_map<int, Ref<Texture2D>>& textureCacheByImageIndex,
            bool async = true,
            std::mutex* p_tex_mutex = nullptr
        ) {
            HN_PROFILE_FUNCTION();
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

            // metallicFactor
            mat->set_metallic_factor((float)gm.pbrMetallicRoughness.metallicFactor);
            // roughnessFactor
            mat->set_roughness_factor((float)gm.pbrMetallicRoughness.roughnessFactor);

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

            {
                auto lk = p_tex_mutex
                    ? std::unique_lock<std::mutex>(*p_tex_mutex)
                    : std::unique_lock<std::mutex>();
                auto it = textureCacheByImageIndex.find(gt.source);
                if (it != textureCacheByImageIndex.end()) {
                    mat->set_base_color_texture(it->second);
                    return mat;
                }
            }

            const tinygltf::Image& img = model.images[(size_t)gt.source];

            if (img.uri.empty() || img.uri.rfind("data:", 0) == 0) {
                if (img.image.empty() || img.width <= 0 || img.height <= 0) {
                    HN_CORE_WARN("glTF: embedded image has no decoded pixel data. Using white.");
                    mat->set_base_color_texture(nullptr);
                    return mat;
                }

                const int w = img.width, h = img.height, comp = img.component;
                const int bits = img.bits > 0 ? img.bits : 8;
                if (bits != 8) {
                    HN_CORE_WARN("glTF: embedded image has {} bits/channel (only 8-bit supported). Using white.", bits);
                    mat->set_base_color_texture(nullptr);
                    return mat;
                }

                // Expand to RGBA8
                std::vector<uint8_t> rgba(w * h * 4);
                for (int i = 0; i < w * h; ++i) {
                    const uint8_t* src = img.image.data() + i * comp;
                    rgba[i*4+0] = comp >= 1 ? src[0] : 0;
                    rgba[i*4+1] = comp >= 2 ? src[1] : (comp == 1 ? src[0] : 0);
                    // grey→RGB
                    rgba[i*4+2] = comp >= 3 ? src[2] : (comp == 1 ? src[0] : 0);
                    rgba[i*4+3] = comp == 4 ? src[3] : 255;
                }

                Ref<Texture2D> tex;
                if (async) {
                    // Create a white 1x1 placeholder immediately; defer GPU upload to main thread.
                    tex = Texture2D::create(1, 1);
                    uint32_t white = 0xFFFFFFFFu;
                    tex->set_data(&white, sizeof(white));

                    auto pixels = std::make_shared<std::vector<uint8_t>>(std::move(rgba));
                    const uint32_t tw = (uint32_t)w, th = (uint32_t)h;
                    TaskSystem::enqueue_main([tex, pixels, tw, th]() {
                        tex->resize(tw, th);
                        tex->set_data_streaming(pixels->data(), tw * th * 4);
                    });
                } else {
                    tex = Texture2D::create((uint32_t)w, (uint32_t)h);
                    tex->set_data(rgba.data(), (uint32_t)rgba.size());
                }
                {
                    auto lk = p_tex_mutex
                        ? std::unique_lock<std::mutex>(*p_tex_mutex)
                        : std::unique_lock<std::mutex>();
                    textureCacheByImageIndex.emplace(gt.source, tex);
                }
                mat->set_base_color_texture(tex);
                return mat;
            }

            const std::filesystem::path texPath = gltfDir / img.uri;

            Ref<Texture2D> tex;
            if (async)
                tex = Texture2D::create_async(texPath.string());
            else
                tex = Texture2D::create(texPath.string());

            {
                auto lk = p_tex_mutex
                    ? std::unique_lock<std::mutex>(*p_tex_mutex)
                    : std::unique_lock<std::mutex>();
                textureCacheByImageIndex.emplace(gt.source, tex);
            }
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

        static BufferLayout make_pnuv_layout() {
            return {
                { ShaderDataType::Float3, "a_position" },
                { ShaderDataType::Float3, "a_normal"   },
                { ShaderDataType::Float2, "a_uv"       },
            };
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
            HN_PROFILE_FUNCTION();
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

        static std::optional<ImportedPrimitiveData> extract_primitive_data(
            const tinygltf::Model& model,
            const tinygltf::Mesh& gm,
            const tinygltf::Primitive& prim,
            size_t primIndex,
            const std::filesystem::path& gltfDir,
            const GltfLoadOptions& options,
            std::unordered_map<int, Ref<Texture2D>>& textureCacheByImageIndex,
            bool async = true,
            std::mutex* p_tex_mutex = nullptr) {
            HN_PROFILE_FUNCTION();

            ImportedPrimitiveData out{};

            if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
                HN_CORE_WARN("glTF: skipping primitive (mode != TRIANGLES)");
                return std::nullopt;
            }

            // POSITION
            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) {
                HN_CORE_WARN("glTF: primitive has no POSITION, skipping");
                return std::nullopt;
            }

            const tinygltf::Accessor* posAcc = find_accessor(model, posIt->second);
            if (!posAcc || posAcc->count == 0)
                return std::nullopt;

            size_t vcount = (size_t)posAcc->count;

            const tinygltf::Accessor* nrmAcc = nullptr;
            if (auto it = prim.attributes.find("NORMAL"); it != prim.attributes.end())
                nrmAcc = find_accessor(model, it->second);

            const tinygltf::Accessor* uvAcc = nullptr;
            if (auto it = prim.attributes.find("TEXCOORD_0"); it != prim.attributes.end())
                uvAcc = find_accessor(model, it->second);

            std::vector<VertexPNUV> vertices(vcount);

            for (size_t i = 0; i < vcount; ++i) {
                if (!read_vec3_float(model, *posAcc, i, vertices[i].position))
                    return std::nullopt;

                if (nrmAcc)
                    read_vec3_float(model, *nrmAcc, i, vertices[i].normal);

                if (uvAcc)
                    read_vec2_float(model, *uvAcc, i, vertices[i].uv);
            }

            // INDICES → ALWAYS uint32_t
            if (prim.indices < 0) {
                HN_CORE_WARN("glTF: primitive has no indices, skipping");
                return std::nullopt;
            }

            const tinygltf::Accessor* idxAcc = find_accessor(model, prim.indices);
            if (!idxAcc || idxAcc->count == 0)
                return std::nullopt;

            size_t idxStride = 0;
            const uint8_t* idxBase = accessor_data_ptr(model, *idxAcc, idxStride);
            if (!idxBase)
                return std::nullopt;

            std::vector<uint32_t> indices(idxAcc->count);

            for (size_t i = 0; i < indices.size(); ++i) {
                if (idxAcc->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    indices[i] = *(idxBase + i * idxStride);
                } else if (idxAcc->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    indices[i] = *reinterpret_cast<const uint16_t*>(idxBase + i * idxStride);
                } else if (idxAcc->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                    indices[i] = *reinterpret_cast<const uint32_t*>(idxBase + i * idxStride);
                } else {
                    HN_CORE_WARN("glTF: unsupported index type");
                    return std::nullopt;
                }
            }

            // MATERIAL
            out.material = build_material_from_gltf(
                model,
                prim.material,
                gltfDir,
                options,
                textureCacheByImageIndex,
                async,
                p_tex_mutex
            );

            out.vertices = std::move(vertices);
            out.indices  = std::move(indices);

            out.name = gm.name.empty()
                ? ("Mesh_" + std::to_string(primIndex))
                : gm.name;

            return out;
        }

        struct MeshletBuildResult {
            MeshletGeometry              geometry;      // counts filled; offsets set by caller
            std::vector<VertexPNUV>      opt_vertices;
            std::vector<uint32_t>        opt_indices;
            std::vector<meshopt_Meshlet> meshlets;
            std::vector<uint32_t>        meshlet_vertices;
            std::vector<uint8_t>         meshlet_triangles;
            std::vector<MeshletBounds>   bounds;
        };

        static std::optional<MeshletBuildResult> build_meshlet_geometry(
              const std::vector<VertexPNUV>& vertices,
              const std::vector<uint32_t>& indices)
        {
            HN_PROFILE_FUNCTION();

            if (vertices.empty() || indices.empty())
                return std::nullopt;

            constexpr size_t kMaxVertices  = 64;
            constexpr size_t kMaxTriangles = 124;
            constexpr float  kConeWeight   = 0.0f;

            // Mutable copies for optimization passes
            std::vector<uint32_t>  opt_indices  = indices;
            std::vector<VertexPNUV> opt_vertices = vertices;

            const size_t vertex_stride = sizeof(VertexPNUV);

            // 1. Reorder indices for post-transform vertex cache efficiency
            {
                HN_PROFILE_SCOPE("meshopt_optimizeVertexCache");
                meshopt_optimizeVertexCache(
                    opt_indices.data(), opt_indices.data(), opt_indices.size(), opt_vertices.size());
            }

            // 2. Reorder indices to reduce pixel overdraw (threshold 1.05 = slight bias toward cache)
            {
                HN_PROFILE_SCOPE("meshopt_optimizeOverdraw");
                meshopt_optimizeOverdraw(
                    opt_indices.data(), opt_indices.data(), opt_indices.size(),
                    &opt_vertices[0].position.x, opt_vertices.size(), vertex_stride, 1.05f);
            }

            // 3. Reorder vertices to match index access order, minimizing vertex fetch overhead
            {
                HN_PROFILE_SCOPE("meshopt_optimizeVertexFetch");
                meshopt_optimizeVertexFetch(
                    opt_vertices.data(), opt_indices.data(), opt_indices.size(),
                    opt_vertices.data(), opt_vertices.size(), vertex_stride);
            }

            const float* positions = &opt_vertices[0].position.x;

            const size_t max_meshlets =
                meshopt_buildMeshletsBound(opt_indices.size(), kMaxVertices, kMaxTriangles);

            std::vector<meshopt_Meshlet> meshlets(max_meshlets);
            std::vector<uint32_t> meshlet_vertices(max_meshlets * kMaxVertices);
            std::vector<uint8_t> meshlet_triangles(max_meshlets * kMaxTriangles * 3);

            size_t meshlet_count;
            {
                HN_PROFILE_SCOPE("meshopt_buildMeshlets");
                meshlet_count = meshopt_buildMeshlets(
                    meshlets.data(),
                    meshlet_vertices.data(),
                    meshlet_triangles.data(),
                    opt_indices.data(),
                    opt_indices.size(),
                    positions,
                    opt_vertices.size(),
                    vertex_stride,
                    kMaxVertices,
                    kMaxTriangles,
                    kConeWeight
                );
            }

            if (meshlet_count == 0)
                return std::nullopt;

            meshlets.resize(meshlet_count);

            size_t used_vertex_refs = 0;
            size_t used_triangle_bytes = 0;

            for (size_t i = 0; i < meshlet_count; ++i) {
                const meshopt_Meshlet& m = meshlets[i];
                used_vertex_refs = std::max(used_vertex_refs, size_t(m.vertex_offset + m.vertex_count));
                used_triangle_bytes = std::max(
                    used_triangle_bytes,
                    size_t(m.triangle_offset + ((m.triangle_count * 3 + 3) & ~3))
                );
            }

            meshlet_vertices.resize(used_vertex_refs);
            meshlet_triangles.resize(used_triangle_bytes);

            // 4. Optimize vertex/triangle order within each meshlet for vertex cache
            for (size_t i = 0; i < meshlet_count; ++i) {
                const meshopt_Meshlet& m = meshlets[i];
                meshopt_optimizeMeshlet(
                    &meshlet_vertices[m.vertex_offset],
                    &meshlet_triangles[m.triangle_offset],
                    m.triangle_count,
                    m.vertex_count
                );
            }

            std::vector<MeshletBounds> bounds(meshlet_count);

            for (size_t i = 0; i < meshlet_count; ++i) {
                const meshopt_Meshlet& m = meshlets[i];

                const meshopt_Bounds b = meshopt_computeMeshletBounds(
                    &meshlet_vertices[m.vertex_offset],
                    &meshlet_triangles[m.triangle_offset],
                    m.triangle_count,
                    positions,
                    opt_vertices.size(),
                    vertex_stride
                );

                bounds[i].center = { b.center[0], b.center[1], b.center[2] };
                bounds[i].radius = b.radius;
                bounds[i].cone_axis_s8[0] = b.cone_axis_s8[0];
                bounds[i].cone_axis_s8[1] = b.cone_axis_s8[1];
                bounds[i].cone_axis_s8[2] = b.cone_axis_s8[2];
                bounds[i].cone_cutoff_s8  = b.cone_cutoff_s8;
            }

            MeshletBuildResult result{};
            result.opt_vertices       = std::move(opt_vertices);
            result.opt_indices        = std::move(opt_indices);
            result.meshlets           = std::move(meshlets);
            result.meshlet_vertices   = std::move(meshlet_vertices);
            result.meshlet_triangles  = std::move(meshlet_triangles);
            result.bounds             = std::move(bounds);

            result.geometry.meshlet_count            = static_cast<uint32_t>(meshlet_count);
            result.geometry.max_vertices_per_meshlet = static_cast<uint32_t>(kMaxVertices);
            result.geometry.max_triangles_per_meshlet= static_cast<uint32_t>(kMaxTriangles);
            // offsets (meshlets_offset etc.) are set by the caller after concatenation

            return result;
        }

        // Shared PrimResult type used by both single-node and multi-node (load_gltf_mesh) paths.
        struct PrimResult {
            Submesh submesh;
            std::optional<MeshletBuildResult> meshlet_build;
        };

        // Pass 1: collect PrimResults for a single glTF mesh node (no SSBO creation).
        static void collect_prims_for_gltf_mesh(
            const tinygltf::Model& model,
            int gltfMeshIndex,
            const glm::mat4& worldTransform,
            const std::filesystem::path& gltfDir,
            const GltfLoadOptions& options,
            std::unordered_map<int, Ref<Texture2D>>& textureCacheByImageIndex,
            bool async,
            std::vector<PrimResult>& out_prims,
            std::mutex* p_tex_mutex = nullptr
        ) {
            HN_PROFILE_FUNCTION();
            if (gltfMeshIndex < 0 || gltfMeshIndex >= (int)model.meshes.size())
                return;

            const tinygltf::Mesh& gm = model.meshes[(size_t)gltfMeshIndex];

            // --- First pass: build per-primitive geometry ---
            for (size_t primIndex = 0; primIndex < gm.primitives.size(); ++primIndex) {
                const tinygltf::Primitive& prim = gm.primitives[primIndex];

                if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
                    HN_CORE_WARN("glTF: skipping primitive (mode != TRIANGLES)");
                    continue;
                }

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

                auto primDataOpt = extract_primitive_data(
                    model, gm, prim, primIndex, gltfDir, options, textureCacheByImageIndex, async, p_tex_mutex);

                if (!primDataOpt)
                    continue;

                const auto& primData = *primDataOpt;

                PrimResult pr{};
                pr.submesh.material  = primData.material;
                pr.submesh.name      = primData.name;
                pr.submesh.transform = worldTransform;

                if (auto result = build_meshlet_geometry(primData.vertices, primData.indices)) {
                    // VAO for classic fallback — plain VB, separate from the meshlet SSBO
                    Ref<VertexArray> vao = VertexArray::create();
                    Ref<VertexBuffer> vb = VertexBuffer::create(
                        (uint32_t)(result->opt_vertices.size() * sizeof(VertexPNUV)));
                    vb->set_data(result->opt_vertices.data(),
                                 (uint32_t)(result->opt_vertices.size() * sizeof(VertexPNUV)));
                    set_default_layout_pnuv(vb);
                    vao->add_vertex_buffer(vb);
                    vao->set_index_buffer(IndexBuffer::create(
                        const_cast<uint32_t*>(result->opt_indices.data()),
                        (uint32_t)result->opt_indices.size()
                    ));
                    pr.submesh.vao = vao;

                    //HN_CORE_INFO("Built {} meshlets for submesh '{}'",
                    //             result->geometry.meshlet_count, pr.submesh.name);
                    pr.submesh.meshlets = result->geometry; // offsets will be filled in second pass
                    pr.meshlet_build    = std::move(*result);
                } else {
                    Ref<VertexArray> vao = VertexArray::create();
                    Ref<VertexBuffer> vb = VertexBuffer::create(
                        (uint32_t)(primData.vertices.size() * sizeof(VertexPNUV)));
                    vb->set_data(primData.vertices.data(),
                                 (uint32_t)(primData.vertices.size() * sizeof(VertexPNUV)));
                    set_default_layout_pnuv(vb);
                    vao->add_vertex_buffer(vb);
                    vao->set_index_buffer(IndexBuffer::create(
                        const_cast<uint32_t*>(primData.indices.data()),
                        (uint32_t)primData.indices.size()
                    ));
                    pr.submesh.vao = vao;
                }

                out_prims.push_back(std::move(pr));
            }
        }

        // Pass 2: concatenate all collected PrimResults into Mesh-level global SSBOs.
        // Sets out->meshlet_buffers and adds all submeshes to out.
        static void finalize_meshlet_buffers(
            Ref<Mesh>& out,
            std::vector<PrimResult>& all_prims
        ) {
            HN_PROFILE_FUNCTION();
            bool has_any_meshlets = false;
            for (const auto& pr : all_prims)
                if (pr.meshlet_build) { has_any_meshlets = true; break; }

            if (has_any_meshlets) {
                std::vector<float>           global_vertices;
                std::vector<meshopt_Meshlet> global_meshlets;
                std::vector<uint32_t>        global_meshlet_vertices;
                std::vector<uint8_t>         global_meshlet_triangles;
                std::vector<MeshletBounds>   global_bounds;

                uint32_t vertex_cursor            = 0;
                uint32_t meshlets_cursor          = 0;
                uint32_t meshlet_vertices_cursor  = 0;
                uint32_t meshlet_triangles_cursor = 0;

                for (auto& pr : all_prims) {
                    if (!pr.meshlet_build) continue;
                    auto& mb = *pr.meshlet_build;

                    const uint32_t v_off  = vertex_cursor;
                    const uint32_t m_off  = meshlets_cursor;
                    const uint32_t mv_off = meshlet_vertices_cursor;
                    const uint32_t mt_off = meshlet_triangles_cursor;

                    const float* v_floats = reinterpret_cast<const float*>(mb.opt_vertices.data());
                    global_vertices.insert(global_vertices.end(),
                        v_floats, v_floats + mb.opt_vertices.size() * 8);
                    vertex_cursor += (uint32_t)mb.opt_vertices.size();

                    for (auto m : mb.meshlets) {
                        m.vertex_offset   += mv_off;
                        m.triangle_offset += mt_off;
                        global_meshlets.push_back(m);
                    }
                    meshlets_cursor += (uint32_t)mb.meshlets.size();

                    for (uint32_t vi : mb.meshlet_vertices)
                        global_meshlet_vertices.push_back(vi + v_off);
                    meshlet_vertices_cursor += (uint32_t)mb.meshlet_vertices.size();

                    global_meshlet_triangles.insert(global_meshlet_triangles.end(),
                        mb.meshlet_triangles.begin(), mb.meshlet_triangles.end());
                    meshlet_triangles_cursor += (uint32_t)mb.meshlet_triangles.size();

                    global_bounds.insert(global_bounds.end(),
                        mb.bounds.begin(), mb.bounds.end());

                    pr.submesh.meshlets->vertex_offset            = v_off;
                    pr.submesh.meshlets->meshlets_offset          = m_off;
                    pr.submesh.meshlets->meshlet_vertices_offset  = mv_off;
                    pr.submesh.meshlets->meshlet_triangles_offset = mt_off;
                    pr.submesh.meshlets->bounds_offset            = m_off;
                }

                GlobalMeshletBuffers global_bufs{};
                global_bufs.vertex_buffer = StorageBuffer::create_from_vector(
                    global_vertices, StorageBufferUsage::Immutable);
                global_bufs.meshlets_buffer = StorageBuffer::create_from_vector(
                    global_meshlets, StorageBufferUsage::Immutable);
                global_bufs.meshlet_vertices_buffer = StorageBuffer::create_from_vector(
                    global_meshlet_vertices, StorageBufferUsage::Immutable);
                global_bufs.meshlet_triangles_buffer = StorageBuffer::create_from_vector(
                    global_meshlet_triangles, StorageBufferUsage::Immutable);
                global_bufs.meshlet_bounds_buffer = StorageBuffer::create_from_vector(
                    global_bounds, StorageBufferUsage::Immutable);

                out->meshlet_buffers = std::move(global_bufs);
            }

            for (auto& pr : all_prims)
                out->add_submesh(std::move(pr.submesh));
        }

        // For build_gltf_node: single-node path — collect and finalize in one shot.
        static void append_mesh_primitives_as_submeshes(
            Ref<Mesh>& out,
            const tinygltf::Model& model,
            int gltfMeshIndex,
            const glm::mat4& worldTransform,
            const std::filesystem::path& gltfDir,
            const GltfLoadOptions& options,
            std::unordered_map<int, Ref<Texture2D>>& textureCacheByImageIndex,
            bool async = true,
            std::mutex* p_tex_mutex = nullptr
        ) {
            HN_PROFILE_FUNCTION();
            std::vector<PrimResult> prims;
            collect_prims_for_gltf_mesh(model, gltfMeshIndex, worldTransform,
                gltfDir, options, textureCacheByImageIndex, async, prims, p_tex_mutex);
            finalize_meshlet_buffers(out, prims);
        }

        // For load_gltf_mesh: traverse the full node tree and collect ALL prims without
        // building SSBOs yet — the caller will call finalize_meshlet_buffers once over all nodes.
        static void traverse_and_collect_prims(
            const tinygltf::Model& model,
            int nodeIndex,
            const glm::mat4& parentWorld,
            const std::filesystem::path& gltfDir,
            const GltfLoadOptions& options,
            std::unordered_map<int, Ref<Texture2D>>& textureCacheByImageIndex,
            bool async,
            std::vector<PrimResult>& out_prims
        ) {
            HN_PROFILE_FUNCTION();
            if (nodeIndex < 0 || nodeIndex >= (int)model.nodes.size())
                return;

            const tinygltf::Node& node = model.nodes[(size_t)nodeIndex];
            const glm::mat4 world = parentWorld * node_local_transform(node);

            if (node.mesh >= 0)
                collect_prims_for_gltf_mesh(model, node.mesh, world,
                    gltfDir, options, textureCacheByImageIndex, async, out_prims);

            for (int child : node.children) {
                traverse_and_collect_prims(model, child, world,
                    gltfDir, options, textureCacheByImageIndex, async, out_prims);
            }
        }

        static GltfNode build_gltf_node(
            const tinygltf::Model& model,
            int node_index,
            const std::filesystem::path& gltf_dir,
            const GltfLoadOptions& options,
            std::unordered_map<int, Ref<Texture2D>>& tex_cache,
            bool async = true,
            std::mutex* p_tex_mutex = nullptr) {

            HN_PROFILE_FUNCTION();

            const tinygltf::Node& n = model.nodes[node_index];

            GltfNode out;
            out.name = n.name.empty() ? ("Node_" + std::to_string(node_index)) : n.name;
            out.local_transform = node_local_transform(n);

            if (n.mesh >= 0) {
                out.mesh = Mesh::create(out.name);
                append_mesh_primitives_as_submeshes(
                    out.mesh, model, n.mesh, glm::mat4(1.0f),
                    gltf_dir, options, tex_cache, async, p_tex_mutex);
                if (out.mesh->empty())
                    out.mesh = nullptr;
            }

            if (!n.children.empty()) {
                out.children.resize(n.children.size());
                auto handle = TaskSystem::parallel_for(
                    0, (uint32_t)n.children.size(),
                    [&](uint32_t i) {
                        out.children[i] = build_gltf_node(
                            model, n.children[i], gltf_dir, options,
                            tex_cache, async, p_tex_mutex);
                    }, 1);
                TaskSystem::wait(handle);
            }

            return out;
        }


    } // namespace

    Ref<Mesh> load_gltf_mesh(const std::filesystem::path& path, const GltfLoadOptions& options, bool async) {
        HN_PROFILE_FUNCTION();
        if (!std::filesystem::exists(path)) {
            HN_CORE_ERROR("load_gltf_mesh: file does not exist: {}", path.string());
            return nullptr;
        }

        tinygltf::Model model;
        tinygltf::TinyGLTF loader;

        // Store raw compressed bytes during parse; we decode in parallel below.
        loader.SetImageLoader(
            [](tinygltf::Image* img, const int, std::string*, std::string*,
               int, int, const unsigned char* bytes, int size, void*) -> bool {
                img->image.assign(bytes, bytes + size);
                img->as_is = true;
                return true;
            }, nullptr);

        std::string err;
        std::string warn;

        bool ok = false;
        {
            HN_PROFILE_SCOPE("load_gltf_mesh::tinygltf_parse");
            const std::string p = path.string();
            if (has_ext(path, ".glb")) {
                ok = loader.LoadBinaryFromFile(&model, &err, &warn, p);
            } else {
                ok = loader.LoadASCIIFromFile(&model, &err, &warn, p);
            }
        }

        if (!warn.empty())
            HN_CORE_WARN("glTF warn: {}", warn);
        if (!err.empty())
            HN_CORE_ERROR("glTF err: {}", err);
        if (!ok) {
            HN_CORE_ERROR("Failed to load glTF: {}", path.string());
            return nullptr;
        }

        // Decode all images in parallel (tinygltf stored raw bytes above)
        if (!model.images.empty()) {
            HN_PROFILE_SCOPE("load_gltf_mesh::parallel_image_decode");
            auto img_handle = TaskSystem::parallel_for(
                0, (uint32_t)model.images.size(),
                [&](uint32_t i) {
                    auto& img = model.images[i];
                    if (!img.as_is || img.image.empty()) return;
                    int w = 0, h = 0, comp = 0;
                    unsigned char* px = stbi_load_from_memory(
                        img.image.data(), (int)img.image.size(), &w, &h, &comp, 0);
                    if (!px) return;
                    img.width     = w;
                    img.height    = h;
                    img.component = comp;
                    img.bits      = 8;
                    img.image.assign(px, px + (size_t)w * h * comp);
                    img.as_is     = false;
                    stbi_image_free(px);
                }, 1);
            TaskSystem::wait(img_handle);
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

        // Collect all primitives from all nodes first, then build SSBOs once.
        // This ensures models with multiple gltf mesh nodes (flight helmet, couch, etc.)
        // get a single GlobalMeshletBuffers covering all their submeshes.
        std::vector<PrimResult> all_prims;
        if (sceneIndex >= 0) {
            const tinygltf::Scene& scene = model.scenes[(size_t)sceneIndex];
            const glm::mat4 I(1.0f);
            for (int rootNode : scene.nodes) {
                traverse_and_collect_prims(model, rootNode, I,
                    gltfDir, options, textureCacheByImageIndex, async, all_prims);
            }
        } else {
            // No scenes? Fallback: emit all meshes at identity.
            HN_CORE_WARN("glTF: model has no scenes; emitting meshes with identity transforms.");
            const glm::mat4 I(1.0f);
            for (int mi = 0; mi < (int)model.meshes.size(); ++mi) {
                collect_prims_for_gltf_mesh(model, mi, I,
                    gltfDir, options, textureCacheByImageIndex, async, all_prims);
            }
        }
        finalize_meshlet_buffers(out, all_prims);

        if (out->empty()) {
            HN_CORE_WARN("load_gltf_mesh: loaded 0 primitives from {}", path.string());
        } else {
            HN_CORE_INFO("load_gltf_mesh: loaded {} submeshes from {}", out->submesh_count(), path.string());
        }

        return out;
    }

    Ref<MeshAsyncHandle> load_gltf_mesh_async(const std::filesystem::path& path,
                                              const GltfLoadOptions& options) {
        HN_PROFILE_FUNCTION();

        auto handle = CreateRef<MeshAsyncHandle>();

        // Kick work to background thread(s)
        TaskSystem::run_async([handle, path, options]() {
            Ref<Mesh> result = load_gltf_mesh(path, options, true);
            if (!result) {
                handle->failed.store(true, std::memory_order_release);
            } else {
                handle->mesh = result;
            }
            handle->done.store(true, std::memory_order_release);
        });

        return handle;
    }

    Ref<GltfSceneTreeAsyncHandle> load_gltf_scene_tree_async(const std::filesystem::path& path,
                                                              const GltfLoadOptions& options) {
        HN_PROFILE_FUNCTION();
        auto handle = CreateRef<GltfSceneTreeAsyncHandle>();
        TaskSystem::run_async([handle, path, options]() {
            GltfSceneTree result = load_gltf_scene_tree(path, options);
            if (result.roots.empty())
                handle->failed.store(true, std::memory_order_release);
            else
                handle->tree = std::move(result);
            handle->done.store(true, std::memory_order_release);
        });
        return handle;
    }

    GltfSceneTree load_gltf_scene_tree(const std::filesystem::path& path, const GltfLoadOptions& options) {
        HN_PROFILE_FUNCTION();
        if (!std::filesystem::exists(path)) {
            HN_CORE_ERROR("load_gltf_scene_tree: file does not exist: {}", path.string());
            return {};
        }

        GltfSceneTree out;
        out.name = path.filename().stem().string();

        tinygltf::Model model;
        tinygltf::TinyGLTF loader;

        // Store raw compressed bytes during parse; decode in parallel below.
        loader.SetImageLoader(
            [](tinygltf::Image* img, const int, std::string*, std::string*,
               int, int, const unsigned char* bytes, int size, void*) -> bool {
                img->image.assign(bytes, bytes + size);
                img->as_is = true;
                return true;
            }, nullptr);

        std::string err;
        std::string warn;

        bool ok = false;
        {
            HN_PROFILE_SCOPE("load_gltf_scene_tree::tinygltf_parse");
            const std::string p = path.string();
            if (has_ext(path, ".glb")) {
                ok = loader.LoadBinaryFromFile(&model, &err, &warn, p);
            } else {
                ok = loader.LoadASCIIFromFile(&model, &err, &warn, p);
            }
        }

        if (!warn.empty())
            HN_CORE_WARN("glTF warn: {}", warn);
        if (!err.empty())
            HN_CORE_ERROR("glTF err: {}", err);
        if (!ok) {
            HN_CORE_ERROR("Failed to load glTF: {}", path.string());
            return {};
        }

        // Decode all images in parallel (tinygltf stored raw bytes above)
        if (!model.images.empty()) {
            HN_PROFILE_SCOPE("load_gltf_scene_tree::parallel_image_decode");
            auto img_handle = TaskSystem::parallel_for(
                0, (uint32_t)model.images.size(),
                [&](uint32_t i) {
                    auto& img = model.images[i];
                    if (!img.as_is || img.image.empty()) return;
                    int w = 0, h = 0, comp = 0;
                    unsigned char* px = stbi_load_from_memory(
                        img.image.data(), (int)img.image.size(), &w, &h, &comp, 0);
                    if (!px) return;
                    img.width     = w;
                    img.height    = h;
                    img.component = comp;
                    img.bits      = 8;
                    img.image.assign(px, px + (size_t)w * h * comp);
                    img.as_is     = false;
                    stbi_image_free(px);
                }, 1);
            TaskSystem::wait(img_handle);
        }

        int scene_index = model.defaultScene;
        if (scene_index < 0 || scene_index >= (int)model.scenes.size()) {
            scene_index = model.scenes.empty() ? -1 : 0;
        }

        std::unordered_map<int, Ref<Texture2D>> tex_cache;
        std::mutex tex_mutex;

        if (scene_index >= 0) {
            const tinygltf::Scene& scene = model.scenes[(size_t)scene_index];

            HN_PROFILE_SCOPE("load_gltf_scene_tree::build_node_tree");
            for (int root_node : scene.nodes) {
                out.roots.push_back(build_gltf_node(model, root_node, path.parent_path(), options, tex_cache, true, &tex_mutex));
            }
        } else {
            // No scenes — emit all meshes as root nodes
            HN_CORE_WARN("glTF: model has no scenes; emitting meshes as root nodes.");
            for (int mi = 0; mi < (int)model.meshes.size(); ++mi) {
                GltfNode node;
                node.name = model.meshes[mi].name.empty() ? ("Mesh_" + std::to_string(mi)) : model.meshes[mi].name;
                node.local_transform = glm::mat4(1.0f);
                node.mesh = Mesh::create(node.name);
                append_mesh_primitives_as_submeshes(node.mesh, model, mi, glm::mat4(1.0f), path.parent_path(), options, tex_cache);
                if (!node.mesh->empty())
                    out.roots.push_back(std::move(node));
            }
        }

        HN_CORE_INFO("load_gltf_scene_tree: loaded {} root nodes from {}", out.roots.size(), path.string());
        return out;
    }

} // namespace Honey