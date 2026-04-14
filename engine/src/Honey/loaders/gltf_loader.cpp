#include "hnpch.h"
#include "gltf_loader.h"
#include "gltf_scene_tree.h"

#include "Honey/core/log.h"
#include "Honey/core/engine.h"
#include "Honey/renderer/buffer.h"
#include "Honey/renderer/renderer.h"
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
                    // Create a white 1x1 placeholder immediately; defer the real GPU upload
                    // to the renderer-owned upload path so the editor thread stays responsive.
                    tex = Texture2D::create(1, 1);
                    uint32_t white = 0xFFFFFFFFu;
                    tex->set_data(&white, sizeof(white));

                    auto pixels = std::make_shared<std::vector<uint8_t>>(std::move(rgba));
                    const uint32_t tw = (uint32_t)w, th = (uint32_t)h;
                    auto upload = [tex, pixels, tw, th]() {
                        tex->resize(tw, th);
                        tex->set_data_streaming(pixels->data(), tw * th * 4);
                    };

                    if (Renderer::get_api() == RendererAPI::API::vulkan) {
                        Application::get().get_vulkan_backend().enqueue_upload_job(std::move(upload));
                    } else {
                        TaskSystem::enqueue_main(std::move(upload));
                    }
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

        static bool parse_gltf_model(const std::filesystem::path& path, tinygltf::Model& out_model) {
            HN_PROFILE_FUNCTION();
            if (!std::filesystem::exists(path)) {
                HN_CORE_ERROR("glTF: file does not exist: {}", path.string());
                return false;
            }

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
                HN_PROFILE_SCOPE("parse_gltf_model::tinygltf_parse");
                const std::string p = path.string();
                if (has_ext(path, ".glb")) {
                    ok = loader.LoadBinaryFromFile(&out_model, &err, &warn, p);
                } else {
                    ok = loader.LoadASCIIFromFile(&out_model, &err, &warn, p);
                }
            }

            if (!warn.empty())
                HN_CORE_WARN("glTF warn: {}", warn);
            if (!err.empty())
                HN_CORE_ERROR("glTF err: {}", err);
            if (!ok) {
                HN_CORE_ERROR("Failed to load glTF: {}", path.string());
                return false;
            }

            if (!out_model.images.empty()) {
                HN_PROFILE_SCOPE("parse_gltf_model::parallel_image_decode");
                auto img_handle = TaskSystem::parallel_for(
                    0, (uint32_t)out_model.images.size(),
                    [&](uint32_t i) {
                        auto& img = out_model.images[i];
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

            return true;
        }

        struct PendingMaterialPayload {
            glm::vec4 base_color_factor{1.0f};
            float metallic_factor = 1.0f;
            float roughness_factor = 1.0f;
            int texture_source = -1;
            std::shared_ptr<DecodedImageRGBA8> base_color_texture{};
        };

        struct PendingSubmeshPayload {
            PendingMaterialPayload material{};
            std::string name;
            glm::mat4 transform{1.0f};
            std::vector<VertexPNUV> vertices;
            std::vector<uint32_t> indices;
            std::optional<MeshletGeometry> meshlets;
        };

        struct PendingMeshletBuffersPayload {
            std::vector<float> vertices;
            std::vector<meshopt_Meshlet> meshlets;
            std::vector<uint32_t> meshlet_vertices;
            std::vector<uint8_t> meshlet_triangles;
            std::vector<MeshletBounds> bounds;
        };

        struct PendingMeshPayload {
            std::string name;
            std::vector<PendingSubmeshPayload> submeshes;
            std::optional<PendingMeshletBuffersPayload> meshlet_buffers;
        };

        struct PendingSceneMeshJob {
            uint32_t node_index = 0;
            int gltf_mesh_index = -1;
            std::string mesh_name;
        };

        struct PendingSceneNode {
            std::string name;
            glm::mat4 local_transform{1.0f};
            std::vector<uint32_t> children;
            int mesh_job_index = -1;
        };

        struct PendingSceneTreePayload {
            std::string name;
            std::vector<PendingSceneNode> nodes;
            std::vector<uint32_t> roots;
            std::vector<std::optional<PendingMeshPayload>> mesh_payloads;
        };

        static std::shared_ptr<DecodedImageRGBA8> decode_gltf_image_rgba8(const tinygltf::Image& img,
                                                                          const std::filesystem::path& gltfDir) {
            HN_PROFILE_FUNCTION();

            auto decoded = std::make_shared<DecodedImageRGBA8>();

            if (!img.image.empty() && img.width > 0 && img.height > 0) {
                const int w = img.width;
                const int h = img.height;
                const int comp = img.component;
                const int bits = img.bits > 0 ? img.bits : 8;
                if (bits != 8 || comp <= 0) {
                    decoded->error = "unsupported embedded image format";
                    return decoded;
                }

                decoded->width = static_cast<uint32_t>(w);
                decoded->height = static_cast<uint32_t>(h);
                decoded->pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
                for (int i = 0; i < w * h; ++i) {
                    const uint8_t* src = img.image.data() + i * comp;
                    decoded->pixels[i * 4 + 0] = comp >= 1 ? src[0] : 0;
                    decoded->pixels[i * 4 + 1] = comp >= 2 ? src[1] : (comp == 1 ? src[0] : 0);
                    decoded->pixels[i * 4 + 2] = comp >= 3 ? src[2] : (comp == 1 ? src[0] : 0);
                    decoded->pixels[i * 4 + 3] = comp == 4 ? src[3] : 255;
                }
                return decoded;
            }

            if (img.uri.empty() || img.uri.rfind("data:", 0) == 0) {
                decoded->error = "image has no decodable payload";
                return decoded;
            }

            const std::filesystem::path texPath = gltfDir / img.uri;
            int w = 0, h = 0, channels = 0;
            stbi_uc* pixels = stbi_load(texPath.string().c_str(), &w, &h, &channels, STBI_rgb_alpha);
            if (!pixels) {
                decoded->error = "stbi_load failed";
                return decoded;
            }

            decoded->width = static_cast<uint32_t>(w);
            decoded->height = static_cast<uint32_t>(h);
            decoded->pixels.resize(decoded->width * decoded->height * 4);
            std::memcpy(decoded->pixels.data(), pixels, decoded->pixels.size());
            stbi_image_free(pixels);
            return decoded;
        }

        static PendingMaterialPayload build_material_payload_from_gltf(
            const tinygltf::Model& model,
            int materialIndex,
            const std::filesystem::path& gltfDir,
            const GltfLoadOptions& options,
            std::unordered_map<int, std::shared_ptr<DecodedImageRGBA8>>& texturePayloadCacheByImageIndex,
            std::mutex* p_tex_mutex = nullptr
        ) {
            HN_PROFILE_FUNCTION();

            PendingMaterialPayload mat{};
            mat.base_color_factor = glm::vec4(1.0f);
            mat.metallic_factor = 1.0f;
            mat.roughness_factor = 1.0f;

            if (materialIndex < 0 || materialIndex >= (int)model.materials.size()) {
                return mat;
            }

            const tinygltf::Material& gm = model.materials[(size_t)materialIndex];
            if (gm.pbrMetallicRoughness.baseColorFactor.size() == 4) {
                mat.base_color_factor.r = (float)gm.pbrMetallicRoughness.baseColorFactor[0];
                mat.base_color_factor.g = (float)gm.pbrMetallicRoughness.baseColorFactor[1];
                mat.base_color_factor.b = (float)gm.pbrMetallicRoughness.baseColorFactor[2];
                mat.base_color_factor.a = (float)gm.pbrMetallicRoughness.baseColorFactor[3];
            }

            mat.metallic_factor = (float)gm.pbrMetallicRoughness.metallicFactor;
            mat.roughness_factor = (float)gm.pbrMetallicRoughness.roughnessFactor;

            if (options.disable_textures) {
                return mat;
            }

            const tinygltf::TextureInfo& ti = gm.pbrMetallicRoughness.baseColorTexture;
            if (ti.index < 0 || ti.index >= (int)model.textures.size()) {
                return mat;
            }

            const tinygltf::Texture& gt = model.textures[(size_t)ti.index];
            if (gt.source < 0 || gt.source >= (int)model.images.size()) {
                return mat;
            }

            mat.texture_source = gt.source;

            {
                auto lk = p_tex_mutex
                    ? std::unique_lock<std::mutex>(*p_tex_mutex)
                    : std::unique_lock<std::mutex>();
                auto it = texturePayloadCacheByImageIndex.find(gt.source);
                if (it != texturePayloadCacheByImageIndex.end()) {
                    mat.base_color_texture = it->second;
                    return mat;
                }
            }

            const tinygltf::Image& img = model.images[(size_t)gt.source];
            auto decoded = decode_gltf_image_rgba8(img, gltfDir);
            if (!decoded || !decoded->ok()) {
                if (decoded && !decoded->error.empty()) {
                    HN_CORE_WARN("glTF: failed to decode texture source {} ({})", gt.source, decoded->error);
                }
                return mat;
            }

            {
                auto lk = p_tex_mutex
                    ? std::unique_lock<std::mutex>(*p_tex_mutex)
                    : std::unique_lock<std::mutex>();
                auto [it, inserted] = texturePayloadCacheByImageIndex.emplace(gt.source, decoded);
                mat.base_color_texture = it->second;
            }

            return mat;
        }

        struct ImportedPrimitivePayload {
            std::string name;
            std::vector<VertexPNUV> vertices;
            std::vector<uint32_t> indices;
            PendingMaterialPayload material;
        };

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

        static std::optional<ImportedPrimitivePayload> extract_primitive_payload(
            const tinygltf::Model& model,
            const tinygltf::Mesh& gm,
            const tinygltf::Primitive& prim,
            size_t primIndex,
            const std::filesystem::path& gltfDir,
            const GltfLoadOptions& options,
            std::unordered_map<int, std::shared_ptr<DecodedImageRGBA8>>& texturePayloadCacheByImageIndex,
            std::mutex* p_tex_mutex = nullptr) {
            HN_PROFILE_FUNCTION();

            ImportedPrimitivePayload out{};

            if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
                HN_CORE_WARN("glTF: skipping primitive (mode != TRIANGLES)");
                return std::nullopt;
            }

            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) {
                HN_CORE_WARN("glTF: primitive has no POSITION, skipping");
                return std::nullopt;
            }

            const tinygltf::Accessor* posAcc = find_accessor(model, posIt->second);
            if (!posAcc || posAcc->count == 0)
                return std::nullopt;

            const tinygltf::Accessor* nrmAcc = nullptr;
            if (auto it = prim.attributes.find("NORMAL"); it != prim.attributes.end())
                nrmAcc = find_accessor(model, it->second);

            const tinygltf::Accessor* uvAcc = nullptr;
            if (auto it = prim.attributes.find("TEXCOORD_0"); it != prim.attributes.end())
                uvAcc = find_accessor(model, it->second);

            std::vector<VertexPNUV> vertices((size_t)posAcc->count);
            for (size_t i = 0; i < vertices.size(); ++i) {
                if (!read_vec3_float(model, *posAcc, i, vertices[i].position))
                    return std::nullopt;
                if (nrmAcc)
                    read_vec3_float(model, *nrmAcc, i, vertices[i].normal);
                if (uvAcc)
                    read_vec2_float(model, *uvAcc, i, vertices[i].uv);
            }

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

            out.material = build_material_payload_from_gltf(
                model,
                prim.material,
                gltfDir,
                options,
                texturePayloadCacheByImageIndex,
                p_tex_mutex
            );
            out.vertices = std::move(vertices);
            out.indices = std::move(indices);
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

        static std::optional<PendingMeshPayload> build_pending_mesh_payload_for_gltf_mesh(
            const tinygltf::Model& model,
            int gltfMeshIndex,
            const glm::mat4& worldTransform,
            const std::filesystem::path& gltfDir,
            const GltfLoadOptions& options,
            std::unordered_map<int, std::shared_ptr<DecodedImageRGBA8>>& texturePayloadCacheByImageIndex,
            std::mutex* p_tex_mutex = nullptr,
            std::string meshName = {}) {
            HN_PROFILE_FUNCTION();

            if (gltfMeshIndex < 0 || gltfMeshIndex >= (int)model.meshes.size())
                return std::nullopt;

            const tinygltf::Mesh& gm = model.meshes[(size_t)gltfMeshIndex];

            struct PendingBuildResult {
                PendingSubmeshPayload submesh;
                std::optional<MeshletBuildResult> meshlet_build;
            };

            PendingMeshPayload out{};
            out.name = meshName.empty() ? gm.name : meshName;

            std::vector<PendingBuildResult> builds;
            for (size_t primIndex = 0; primIndex < gm.primitives.size(); ++primIndex) {
                const tinygltf::Primitive& prim = gm.primitives[primIndex];
                auto primDataOpt = extract_primitive_payload(
                    model, gm, prim, primIndex, gltfDir, options, texturePayloadCacheByImageIndex, p_tex_mutex);
                if (!primDataOpt)
                    continue;

                const auto& primData = *primDataOpt;
                PendingBuildResult build{};
                build.submesh.material = primData.material;
                build.submesh.name = primData.name;
                build.submesh.transform = worldTransform;

                if (auto result = build_meshlet_geometry(primData.vertices, primData.indices)) {
                    build.submesh.vertices = std::move(result->opt_vertices);
                    build.submesh.indices = std::move(result->opt_indices);
                    build.submesh.meshlets = result->geometry;
                    build.meshlet_build = std::move(*result);
                } else {
                    build.submesh.vertices = primData.vertices;
                    build.submesh.indices = primData.indices;
                }

                builds.push_back(std::move(build));
            }

            if (builds.empty())
                return std::nullopt;

            bool has_any_meshlets = false;
            for (const auto& build : builds) {
                if (build.meshlet_build) {
                    has_any_meshlets = true;
                    break;
                }
            }

            if (has_any_meshlets) {
                PendingMeshletBuffersPayload global{};
                uint32_t vertex_cursor = 0;
                uint32_t meshlets_cursor = 0;
                uint32_t meshlet_vertices_cursor = 0;
                uint32_t meshlet_triangles_cursor = 0;

                for (auto& build : builds) {
                    if (!build.meshlet_build)
                        continue;

                    auto& mb = *build.meshlet_build;
                    const uint32_t v_off = vertex_cursor;
                    const uint32_t m_off = meshlets_cursor;
                    const uint32_t mv_off = meshlet_vertices_cursor;
                    const uint32_t mt_off = meshlet_triangles_cursor;

                    const float* v_floats = reinterpret_cast<const float*>(build.submesh.vertices.data());
                    global.vertices.insert(global.vertices.end(),
                        v_floats, v_floats + build.submesh.vertices.size() * 8);
                    vertex_cursor += (uint32_t)build.submesh.vertices.size();

                    for (auto m : mb.meshlets) {
                        m.vertex_offset += mv_off;
                        m.triangle_offset += mt_off;
                        global.meshlets.push_back(m);
                    }
                    meshlets_cursor += (uint32_t)mb.meshlets.size();

                    for (uint32_t vi : mb.meshlet_vertices)
                        global.meshlet_vertices.push_back(vi + v_off);
                    meshlet_vertices_cursor += (uint32_t)mb.meshlet_vertices.size();

                    global.meshlet_triangles.insert(global.meshlet_triangles.end(),
                        mb.meshlet_triangles.begin(), mb.meshlet_triangles.end());
                    meshlet_triangles_cursor += (uint32_t)mb.meshlet_triangles.size();

                    global.bounds.insert(global.bounds.end(), mb.bounds.begin(), mb.bounds.end());

                    build.submesh.meshlets->vertex_offset = v_off;
                    build.submesh.meshlets->meshlets_offset = m_off;
                    build.submesh.meshlets->meshlet_vertices_offset = mv_off;
                    build.submesh.meshlets->meshlet_triangles_offset = mt_off;
                    build.submesh.meshlets->bounds_offset = m_off;
                }

                out.meshlet_buffers = std::move(global);
            }

            out.submeshes.reserve(builds.size());
            for (auto& build : builds)
                out.submeshes.push_back(std::move(build.submesh));

            return out;
        }

        static Ref<Texture2D> finalize_texture_payload(
            const std::shared_ptr<DecodedImageRGBA8>& decoded) {
            if (!decoded || !decoded->ok())
                return nullptr;

            Ref<Texture2D> tex = Texture2D::create(decoded->width, decoded->height);
            tex->set_data(decoded->pixels.data(), static_cast<uint32_t>(decoded->pixels.size()));
            return tex;
        }

        static Ref<Material> finalize_material_payload(
            const PendingMaterialPayload& payload,
            std::unordered_map<int, Ref<Texture2D>>& textureCacheByImageIndex) {
            Ref<Material> mat = Material::create();
            mat->set_base_color_factor(payload.base_color_factor);
            mat->set_metallic_factor(payload.metallic_factor);
            mat->set_roughness_factor(payload.roughness_factor);

            if (payload.texture_source >= 0 && payload.base_color_texture) {
                auto it = textureCacheByImageIndex.find(payload.texture_source);
                if (it == textureCacheByImageIndex.end()) {
                    Ref<Texture2D> tex = finalize_texture_payload(payload.base_color_texture);
                    if (tex) {
                        it = textureCacheByImageIndex.emplace(payload.texture_source, tex).first;
                    }
                }
                if (it != textureCacheByImageIndex.end())
                    mat->set_base_color_texture(it->second);
            }

            return mat;
        }

        static Ref<Mesh> finalize_pending_mesh_payload(
            const PendingMeshPayload& payload,
            std::unordered_map<int, Ref<Texture2D>>& textureCacheByImageIndex) {
            HN_PROFILE_FUNCTION();

            Ref<Mesh> out = Mesh::create(payload.name);
            for (const auto& submeshPayload : payload.submeshes) {
                Submesh submesh{};
                submesh.material = finalize_material_payload(submeshPayload.material, textureCacheByImageIndex);
                submesh.name = submeshPayload.name;
                submesh.transform = submeshPayload.transform;
                submesh.meshlets = submeshPayload.meshlets;

                Ref<VertexArray> vao = VertexArray::create();
                Ref<VertexBuffer> vb = VertexBuffer::create(
                    static_cast<uint32_t>(submeshPayload.vertices.size() * sizeof(VertexPNUV)));
                vb->set_data(submeshPayload.vertices.data(),
                             static_cast<uint32_t>(submeshPayload.vertices.size() * sizeof(VertexPNUV)));
                set_default_layout_pnuv(vb);
                vao->add_vertex_buffer(vb);
                vao->set_index_buffer(IndexBuffer::create(
                    const_cast<uint32_t*>(submeshPayload.indices.data()),
                    static_cast<uint32_t>(submeshPayload.indices.size())));
                submesh.vao = vao;

                out->add_submesh(std::move(submesh));
            }

            if (payload.meshlet_buffers &&
                !payload.meshlet_buffers->vertices.empty() &&
                !payload.meshlet_buffers->meshlets.empty() &&
                !payload.meshlet_buffers->meshlet_vertices.empty() &&
                !payload.meshlet_buffers->meshlet_triangles.empty() &&
                !payload.meshlet_buffers->bounds.empty()) {
                GlobalMeshletBuffers global_bufs{};
                global_bufs.vertex_buffer = StorageBuffer::create_from_vector(
                    payload.meshlet_buffers->vertices, StorageBufferUsage::Immutable);
                global_bufs.meshlets_buffer = StorageBuffer::create_from_vector(
                    payload.meshlet_buffers->meshlets, StorageBufferUsage::Immutable);
                global_bufs.meshlet_vertices_buffer = StorageBuffer::create_from_vector(
                    payload.meshlet_buffers->meshlet_vertices, StorageBufferUsage::Immutable);
                global_bufs.meshlet_triangles_buffer = StorageBuffer::create_from_vector(
                    payload.meshlet_buffers->meshlet_triangles, StorageBufferUsage::Immutable);
                global_bufs.meshlet_bounds_buffer = StorageBuffer::create_from_vector(
                    payload.meshlet_buffers->bounds, StorageBufferUsage::Immutable);
                out->meshlet_buffers = std::move(global_bufs);
            } else if (payload.meshlet_buffers) {
                HN_CORE_WARN("Skipping empty meshlet buffer payload for mesh '{}'", payload.name);
            }

            return out->empty() ? nullptr : out;
        }

        static uint32_t append_pending_scene_node(
            const tinygltf::Model& model,
            int node_index,
            PendingSceneTreePayload& out,
            std::vector<PendingSceneMeshJob>& mesh_jobs) {
            const tinygltf::Node& n = model.nodes[(size_t)node_index];

            PendingSceneNode node{};
            node.name = n.name.empty() ? ("Node_" + std::to_string(node_index)) : n.name;
            node.local_transform = node_local_transform(n);

            const uint32_t current_index = static_cast<uint32_t>(out.nodes.size());
            out.nodes.push_back(std::move(node));

            if (n.mesh >= 0) {
                out.nodes[current_index].mesh_job_index = static_cast<int>(mesh_jobs.size());
                mesh_jobs.push_back({ current_index, n.mesh, out.nodes[current_index].name });
            }

            out.nodes[current_index].children.reserve(n.children.size());
            for (int child : n.children) {
                const uint32_t child_index =
                    append_pending_scene_node(model, child, out, mesh_jobs);
                out.nodes[current_index].children.push_back(child_index);
            }

            return current_index;
        }

        static PendingSceneTreePayload build_pending_scene_tree_payload(
            const tinygltf::Model& model,
            const std::filesystem::path& path,
            const GltfLoadOptions& options) {
            HN_PROFILE_FUNCTION();

            PendingSceneTreePayload out{};
            out.name = path.filename().stem().string();

            int scene_index = model.defaultScene;
            if (scene_index < 0 || scene_index >= (int)model.scenes.size()) {
                scene_index = model.scenes.empty() ? -1 : 0;
            }

            std::vector<PendingSceneMeshJob> mesh_jobs;
            if (scene_index >= 0) {
                const tinygltf::Scene& scene = model.scenes[(size_t)scene_index];
                out.roots.reserve(scene.nodes.size());
                for (int root_node : scene.nodes) {
                    out.roots.push_back(append_pending_scene_node(model, root_node, out, mesh_jobs));
                }
            } else {
                HN_CORE_WARN("glTF: model has no scenes; emitting meshes as root nodes.");
                out.roots.reserve(model.meshes.size());
                for (int mi = 0; mi < (int)model.meshes.size(); ++mi) {
                    PendingSceneNode node{};
                    node.name = model.meshes[mi].name.empty() ? ("Mesh_" + std::to_string(mi)) : model.meshes[mi].name;
                    node.local_transform = glm::mat4(1.0f);
                    node.mesh_job_index = static_cast<int>(mesh_jobs.size());
                    const uint32_t node_index = static_cast<uint32_t>(out.nodes.size());
                    out.nodes.push_back(std::move(node));
                    out.roots.push_back(node_index);
                    mesh_jobs.push_back({ node_index, mi, out.nodes[node_index].name });
                }
            }

            out.mesh_payloads.resize(mesh_jobs.size());
            if (mesh_jobs.empty())
                return out;

            std::unordered_map<int, std::shared_ptr<DecodedImageRGBA8>> texturePayloadCacheByImageIndex;
            std::mutex texturePayloadMutex;
            const std::filesystem::path gltfDir = path.parent_path();

            auto mesh_handle = TaskSystem::parallel_for(
                0, static_cast<uint32_t>(mesh_jobs.size()),
                [&](uint32_t i) {
                    const auto& job = mesh_jobs[i];
                    out.mesh_payloads[i] = build_pending_mesh_payload_for_gltf_mesh(
                        model,
                        job.gltf_mesh_index,
                        glm::mat4(1.0f),
                        gltfDir,
                        options,
                        texturePayloadCacheByImageIndex,
                        &texturePayloadMutex,
                        job.mesh_name
                    );
                }, 1);
            TaskSystem::wait(mesh_handle);

            return out;
        }

        static GltfNode finalize_pending_scene_node(
            uint32_t node_index,
            const PendingSceneTreePayload& payload,
            std::unordered_map<int, Ref<Texture2D>>& textureCacheByImageIndex) {
            const auto& pending = payload.nodes[node_index];
            GltfNode out{};
            out.name = pending.name;
            out.local_transform = pending.local_transform;

            if (pending.mesh_job_index >= 0) {
                const auto& meshPayload = payload.mesh_payloads[(size_t)pending.mesh_job_index];
                if (meshPayload.has_value()) {
                    out.mesh = finalize_pending_mesh_payload(*meshPayload, textureCacheByImageIndex);
                }
            }

            out.children.reserve(pending.children.size());
            for (uint32_t child_index : pending.children) {
                out.children.push_back(finalize_pending_scene_node(child_index, payload, textureCacheByImageIndex));
            }

            return out;
        }

        static GltfSceneTree finalize_pending_scene_tree_payload(const PendingSceneTreePayload& payload) {
            HN_PROFILE_FUNCTION();

            GltfSceneTree out{};
            out.name = payload.name;

            std::unordered_map<int, Ref<Texture2D>> textureCacheByImageIndex;
            out.roots.reserve(payload.roots.size());
            for (uint32_t root_index : payload.roots) {
                out.roots.push_back(finalize_pending_scene_node(root_index, payload, textureCacheByImageIndex));
            }

            return out;
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
                for (size_t i = 0; i < n.children.size(); ++i) {
                    out.children[i] = build_gltf_node(
                        model, n.children[i], gltf_dir, options,
                        tex_cache, async, p_tex_mutex);
                }
            }

            return out;
        }

        static Ref<Mesh> build_gltf_mesh_from_model(const tinygltf::Model& model,
                                                    const std::filesystem::path& path,
                                                    const GltfLoadOptions& options,
                                                    bool async) {
            HN_PROFILE_FUNCTION();

            Ref<Mesh> out = Mesh::create(path.filename().string());

            const std::filesystem::path gltfDir = path.parent_path();
            std::unordered_map<int, Ref<Texture2D>> textureCacheByImageIndex;

            int sceneIndex = model.defaultScene;
            if (sceneIndex < 0 || sceneIndex >= (int)model.scenes.size()) {
                sceneIndex = model.scenes.empty() ? -1 : 0;
            }

            std::vector<PrimResult> all_prims;
            if (sceneIndex >= 0) {
                const tinygltf::Scene& scene = model.scenes[(size_t)sceneIndex];
                const glm::mat4 I(1.0f);
                for (int rootNode : scene.nodes) {
                    traverse_and_collect_prims(model, rootNode, I,
                        gltfDir, options, textureCacheByImageIndex, async, all_prims);
                }
            } else {
                HN_CORE_WARN("glTF: model has no scenes; emitting meshes with identity transforms.");
                const glm::mat4 I(1.0f);
                for (int mi = 0; mi < (int)model.meshes.size(); ++mi) {
                    collect_prims_for_gltf_mesh(model, mi, I,
                        gltfDir, options, textureCacheByImageIndex, async, all_prims);
                }
            }
            finalize_meshlet_buffers(out, all_prims);

            return out;
        }

        static GltfSceneTree build_gltf_scene_tree_from_model(const tinygltf::Model& model,
                                                              const std::filesystem::path& path,
                                                              const GltfLoadOptions& options) {
            HN_PROFILE_FUNCTION();

            GltfSceneTree out;
            out.name = path.filename().stem().string();

            int scene_index = model.defaultScene;
            if (scene_index < 0 || scene_index >= (int)model.scenes.size()) {
                scene_index = model.scenes.empty() ? -1 : 0;
            }

            std::unordered_map<int, Ref<Texture2D>> tex_cache;
            std::mutex tex_mutex;

            if (scene_index >= 0) {
                const tinygltf::Scene& scene = model.scenes[(size_t)scene_index];

                HN_PROFILE_SCOPE("build_gltf_scene_tree_from_model::build_node_tree");
                for (int root_node : scene.nodes) {
                    out.roots.push_back(build_gltf_node(model, root_node, path.parent_path(), options, tex_cache, true, &tex_mutex));
                }
            } else {
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

            return out;
        }


    } // namespace

    Ref<Mesh> load_gltf_mesh(const std::filesystem::path& path, const GltfLoadOptions& options, bool async) {
        HN_PROFILE_FUNCTION();
        tinygltf::Model model;
        if (!parse_gltf_model(path, model)) {
            HN_CORE_ERROR("load_gltf_mesh: failed to parse {}", path.string());
            return nullptr;
        }
        Ref<Mesh> out = build_gltf_mesh_from_model(model, path, options, async);

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

        TaskSystem::run_async([handle, path, options]() {
            tinygltf::Model model;
            if (!parse_gltf_model(path, model)) {
                handle->failed.store(true, std::memory_order_release);
                handle->done.store(true, std::memory_order_release);
                return;
            }

            auto finalize = [handle, path, options, model = std::move(model)]() mutable {
                Ref<Mesh> result = build_gltf_mesh_from_model(model, path, options, true);
                if (!result)
                    handle->failed.store(true, std::memory_order_release);
                else
                    handle->mesh = std::move(result);
                handle->done.store(true, std::memory_order_release);
            };

            if (Renderer::get_api() == RendererAPI::API::vulkan) {
                Application::get().get_vulkan_backend().enqueue_upload_job(std::move(finalize));
            } else {
                TaskSystem::enqueue_main(std::move(finalize));
            }
        });

        return handle;
    }

    Ref<GltfSceneTreeAsyncHandle> load_gltf_scene_tree_async(const std::filesystem::path& path,
                                                              const GltfLoadOptions& options) {
        HN_PROFILE_FUNCTION();
        auto handle = CreateRef<GltfSceneTreeAsyncHandle>();
        TaskSystem::run_async([handle, path, options]() {
            tinygltf::Model model;
            if (!parse_gltf_model(path, model)) {
                handle->failed.store(true, std::memory_order_release);
                handle->done.store(true, std::memory_order_release);
                return;
            }

            PendingSceneTreePayload pending = build_pending_scene_tree_payload(model, path, options);

            auto finalize = [handle, pending = std::move(pending)]() mutable {
                GltfSceneTree result = finalize_pending_scene_tree_payload(pending);
                if (result.roots.empty())
                    handle->failed.store(true, std::memory_order_release);
                else
                    handle->tree = std::move(result);
                handle->done.store(true, std::memory_order_release);
            };

            if (Renderer::get_api() == RendererAPI::API::vulkan) {
                Application::get().get_vulkan_backend().enqueue_upload_job(std::move(finalize));
            } else {
                TaskSystem::enqueue_main(std::move(finalize));
            }
        });
        return handle;
    }

    GltfSceneTree load_gltf_scene_tree(const std::filesystem::path& path, const GltfLoadOptions& options) {
        HN_PROFILE_FUNCTION();
        tinygltf::Model model;
        if (!parse_gltf_model(path, model)) {
            HN_CORE_ERROR("load_gltf_scene_tree: failed to parse {}", path.string());
            return {};
        }
        GltfSceneTree out = build_gltf_scene_tree_from_model(model, path, options);

        HN_CORE_INFO("load_gltf_scene_tree: loaded {} root nodes from {}", out.roots.size(), path.string());
        return out;
    }

} // namespace Honey
