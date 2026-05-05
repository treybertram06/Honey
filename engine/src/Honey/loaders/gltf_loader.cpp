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
        using packed_vec2 = glm::vec<2, float, glm::packed_highp>;
        using packed_vec3 = glm::vec<3, float, glm::packed_highp>;
        using packed_vec4 = glm::vec<4, float, glm::packed_highp>;

        struct VertexPBR {
            packed_vec3 position{0.0f};
            packed_vec3 normal{0.0f, 0.0f, 1.0f};
            packed_vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
            packed_vec2 uv0{0.0f};
            packed_vec2 uv1{0.0f};
        };
        static constexpr size_t k_vertex_floats = sizeof(VertexPBR) / sizeof(float);
        static_assert(sizeof(VertexPBR) % sizeof(float) == 0, "VertexPBR must be float-packed");
        static_assert(sizeof(VertexPBR) == (14 * sizeof(float)), "VertexPBR size must match shader vertex packing");

        struct ImportedPrimitiveData {
            std::string name;

            std::vector<VertexPBR> vertices;
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

        static bool read_vec4_float(const tinygltf::Model& model, const tinygltf::Accessor& acc, size_t index, glm::vec4& out) {
            if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || acc.type != TINYGLTF_TYPE_VEC4)
                return false;

            size_t stride = 0;
            const uint8_t* base = accessor_data_ptr(model, acc, stride);
            if (!base) return false;

            const float* f = reinterpret_cast<const float*>(base + index * stride);
            out = glm::vec4(f[0], f[1], f[2], f[3]);
            return true;
        }

        static void parse_texture_transform(const tinygltf::ExtensionMap& ext_map, Material::TextureSlot& slot) {
            auto tx_it = ext_map.find("KHR_texture_transform");
            if (tx_it == ext_map.end())
                return;

            const tinygltf::Value& tx = tx_it->second;
            if (!tx.IsObject())
                return;

            auto& t = slot.transform;
            t.has_transform = true;

            if (tx.Has("offset")) {
                const tinygltf::Value& offset = tx.Get("offset");
                if (offset.IsArray() && offset.ArrayLen() >= 2) {
                    t.offset.x = (float)offset.Get(0).GetNumberAsDouble();
                    t.offset.y = (float)offset.Get(1).GetNumberAsDouble();
                }
            }
            if (tx.Has("scale")) {
                const tinygltf::Value& scale = tx.Get("scale");
                if (scale.IsArray() && scale.ArrayLen() >= 2) {
                    t.scale.x = (float)scale.Get(0).GetNumberAsDouble();
                    t.scale.y = (float)scale.Get(1).GetNumberAsDouble();
                }
            }
            if (tx.Has("rotation")) {
                t.rotation = (float)tx.Get("rotation").GetNumberAsDouble();
            }
            if (tx.Has("texCoord")) {
                slot.tex_coord = tx.Get("texCoord").GetNumberAsInt();
            }
        }

        static void parse_texture_transform_from_value(const tinygltf::Value& extensions_value, Material::TextureSlot& slot) {
            if (!extensions_value.IsObject() || !extensions_value.Has("KHR_texture_transform"))
                return;
            const tinygltf::Value& tx = extensions_value.Get("KHR_texture_transform");
            if (!tx.IsObject())
                return;

            auto& t = slot.transform;
            t.has_transform = true;

            if (tx.Has("offset")) {
                const tinygltf::Value& offset = tx.Get("offset");
                if (offset.IsArray() && offset.ArrayLen() >= 2) {
                    t.offset.x = (float)offset.Get(0).GetNumberAsDouble();
                    t.offset.y = (float)offset.Get(1).GetNumberAsDouble();
                }
            }
            if (tx.Has("scale")) {
                const tinygltf::Value& scale = tx.Get("scale");
                if (scale.IsArray() && scale.ArrayLen() >= 2) {
                    t.scale.x = (float)scale.Get(0).GetNumberAsDouble();
                    t.scale.y = (float)scale.Get(1).GetNumberAsDouble();
                }
            }
            if (tx.Has("rotation")) {
                t.rotation = (float)tx.Get("rotation").GetNumberAsDouble();
            }
            if (tx.Has("texCoord")) {
                slot.tex_coord = tx.Get("texCoord").GetNumberAsInt();
            }
        }

        template <typename TTextureInfo>
        static void fill_texture_slot_metadata(
            const tinygltf::Model& model,
            const TTextureInfo& info,
            Material::TextureSlot& slot) {
            slot.tex_coord = info.texCoord;
            slot.gltf_texture_index = info.index;
            parse_texture_transform(info.extensions, slot);

            if (info.index < 0 || info.index >= (int)model.textures.size())
                return;

            const tinygltf::Texture& gt = model.textures[(size_t)info.index];
            slot.gltf_image_source = gt.source;
        }

        static void fill_texture_slot_metadata_from_value(
            const tinygltf::Model& model,
            const tinygltf::Value& texture_info,
            Material::TextureSlot& slot) {
            if (!texture_info.IsObject() || !texture_info.Has("index"))
                return;

            slot.gltf_texture_index = texture_info.Get("index").GetNumberAsInt();
            if (texture_info.Has("texCoord"))
                slot.tex_coord = texture_info.Get("texCoord").GetNumberAsInt();
            if (texture_info.Has("extensions"))
                parse_texture_transform_from_value(texture_info.Get("extensions"), slot);

            if (slot.gltf_texture_index < 0 || slot.gltf_texture_index >= (int)model.textures.size())
                return;
            const tinygltf::Texture& gt = model.textures[(size_t)slot.gltf_texture_index];
            slot.gltf_image_source = gt.source;
        }

        static Ref<Texture2D> load_texture_from_source(
            const tinygltf::Model& model,
            int source,
            const std::filesystem::path& gltfDir,
            bool async,
            std::unordered_map<int, Ref<Texture2D>>& textureCacheByImageIndex,
            std::mutex* p_tex_mutex = nullptr) {
            if (source < 0 || source >= (int)model.images.size())
                return nullptr;

            {
                auto lk = p_tex_mutex
                    ? std::unique_lock<std::mutex>(*p_tex_mutex)
                    : std::unique_lock<std::mutex>();
                auto it = textureCacheByImageIndex.find(source);
                if (it != textureCacheByImageIndex.end())
                    return it->second;
            }

            const tinygltf::Image& img = model.images[(size_t)source];
            Ref<Texture2D> tex;

            if (!img.image.empty() && img.width > 0 && img.height > 0) {
                const int w = img.width, h = img.height, comp = img.component;
                const int bits = img.bits > 0 ? img.bits : 8;
                if (bits != 8 || comp <= 0) {
                    HN_CORE_WARN("glTF: embedded image source {} has unsupported format", source);
                    return nullptr;
                }

                std::vector<uint8_t> rgba(w * h * 4);
                for (int i = 0; i < w * h; ++i) {
                    const uint8_t* src = img.image.data() + i * comp;
                    rgba[i * 4 + 0] = comp >= 1 ? src[0] : 0;
                    rgba[i * 4 + 1] = comp >= 2 ? src[1] : (comp == 1 ? src[0] : 0);
                    rgba[i * 4 + 2] = comp >= 3 ? src[2] : (comp == 1 ? src[0] : 0);
                    rgba[i * 4 + 3] = comp == 4 ? src[3] : 255;
                }

                if (async) {
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
            } else if (!img.uri.empty() && img.uri.rfind("data:", 0) != 0) {
                const std::filesystem::path texPath = gltfDir / img.uri;
                tex = async ? Texture2D::create_async(texPath.string())
                            : Texture2D::create(texPath.string());
            }

            if (!tex)
                return nullptr;

            {
                auto lk = p_tex_mutex
                    ? std::unique_lock<std::mutex>(*p_tex_mutex)
                    : std::unique_lock<std::mutex>();
                textureCacheByImageIndex.emplace(source, tex);
            }
            return tex;
        }

        static void parse_material_extensions(const tinygltf::Model& model, const tinygltf::Material& gm, Material& mat) {
            auto parse_float = [](const tinygltf::Value& v, const char* key, float fallback) -> float {
                if (!v.IsObject() || !v.Has(key))
                    return fallback;
                return (float)v.Get(key).GetNumberAsDouble();
            };
            auto parse_vec3 = [](const tinygltf::Value& v, const char* key, const glm::vec3& fallback) -> glm::vec3 {
                if (!v.IsObject() || !v.Has(key))
                    return fallback;
                const tinygltf::Value& arr = v.Get(key);
                if (!arr.IsArray() || arr.ArrayLen() < 3)
                    return fallback;
                return glm::vec3(
                    (float)arr.Get(0).GetNumberAsDouble(),
                    (float)arr.Get(1).GetNumberAsDouble(),
                    (float)arr.Get(2).GetNumberAsDouble());
            };

            if (gm.extensions.find("KHR_materials_unlit") != gm.extensions.end()) {
                mat.pbr().extensions.unlit.enabled = true;
            }

            if (auto it = gm.extensions.find("KHR_materials_emissive_strength"); it != gm.extensions.end()) {
                mat.pbr().extensions.emissive_strength.enabled = true;
                mat.pbr().extensions.emissive_strength.strength = parse_float(it->second, "emissiveStrength", 1.0f);
            }

            if (auto it = gm.extensions.find("KHR_materials_clearcoat"); it != gm.extensions.end()) {
                auto& cc = mat.pbr().extensions.clearcoat;
                cc.enabled = true;
                cc.factor = parse_float(it->second, "clearcoatFactor", 0.0f);
                cc.roughness_factor = parse_float(it->second, "clearcoatRoughnessFactor", 0.0f);
                cc.normal_scale = 1.0f;
                if (it->second.IsObject()) {
                    fill_texture_slot_metadata_from_value(model, it->second.Get("clearcoatTexture"), cc.texture);
                    fill_texture_slot_metadata_from_value(model, it->second.Get("clearcoatRoughnessTexture"), cc.roughness_texture);
                    fill_texture_slot_metadata_from_value(model, it->second.Get("clearcoatNormalTexture"), cc.normal_texture);
                }
                if (it->second.IsObject() && it->second.Has("clearcoatNormalTexture")) {
                    const tinygltf::Value& n = it->second.Get("clearcoatNormalTexture");
                    if (n.IsObject() && n.Has("scale"))
                        cc.normal_scale = (float)n.Get("scale").GetNumberAsDouble();
                }
            }

            if (auto it = gm.extensions.find("KHR_materials_sheen"); it != gm.extensions.end()) {
                auto& s = mat.pbr().extensions.sheen;
                s.enabled = true;
                s.color_factor = parse_vec3(it->second, "sheenColorFactor", glm::vec3(0.0f));
                s.roughness_factor = parse_float(it->second, "sheenRoughnessFactor", 0.0f);
                if (it->second.IsObject()) {
                    fill_texture_slot_metadata_from_value(model, it->second.Get("sheenColorTexture"), s.color_texture);
                    fill_texture_slot_metadata_from_value(model, it->second.Get("sheenRoughnessTexture"), s.roughness_texture);
                }
            }

            if (auto it = gm.extensions.find("KHR_materials_specular"); it != gm.extensions.end()) {
                auto& s = mat.pbr().extensions.specular;
                s.enabled = true;
                s.specular_factor = parse_float(it->second, "specularFactor", 1.0f);
                s.specular_color_factor = parse_vec3(it->second, "specularColorFactor", glm::vec3(1.0f));
                if (it->second.IsObject()) {
                    fill_texture_slot_metadata_from_value(model, it->second.Get("specularTexture"), s.specular_texture);
                    fill_texture_slot_metadata_from_value(model, it->second.Get("specularColorTexture"), s.specular_color_texture);
                }
            }

            if (auto it = gm.extensions.find("KHR_materials_transmission"); it != gm.extensions.end()) {
                auto& t = mat.pbr().extensions.transmission;
                t.enabled = true;
                t.factor = parse_float(it->second, "transmissionFactor", 0.0f);
                if (it->second.IsObject())
                    fill_texture_slot_metadata_from_value(model, it->second.Get("transmissionTexture"), t.texture);
            }

            if (auto it = gm.extensions.find("KHR_materials_volume"); it != gm.extensions.end()) {
                auto& v = mat.pbr().extensions.volume;
                v.enabled = true;
                v.thickness_factor = parse_float(it->second, "thicknessFactor", 0.0f);
                v.attenuation_distance = parse_float(it->second, "attenuationDistance", 0.0f);
                v.attenuation_color = parse_vec3(it->second, "attenuationColor", glm::vec3(1.0f));
                if (it->second.IsObject())
                    fill_texture_slot_metadata_from_value(model, it->second.Get("thicknessTexture"), v.thickness_texture);
            }

            if (auto it = gm.extensions.find("KHR_materials_ior"); it != gm.extensions.end()) {
                auto& ior = mat.pbr().extensions.ior;
                ior.enabled = true;
                ior.ior = parse_float(it->second, "ior", 1.5f);
            }

            if (auto it = gm.extensions.find("KHR_materials_iridescence"); it != gm.extensions.end()) {
                auto& iri = mat.pbr().extensions.iridescence;
                iri.enabled = true;
                iri.factor = parse_float(it->second, "iridescenceFactor", 0.0f);
                iri.ior = parse_float(it->second, "iridescenceIor", 1.3f);
                iri.thickness_min = parse_float(it->second, "iridescenceThicknessMinimum", 100.0f);
                iri.thickness_max = parse_float(it->second, "iridescenceThicknessMaximum", 400.0f);
                if (it->second.IsObject()) {
                    fill_texture_slot_metadata_from_value(model, it->second.Get("iridescenceTexture"), iri.texture);
                    fill_texture_slot_metadata_from_value(model, it->second.Get("iridescenceThicknessTexture"), iri.thickness_texture);
                }
            }

            if (auto it = gm.extensions.find("KHR_materials_anisotropy"); it != gm.extensions.end()) {
                auto& aniso = mat.pbr().extensions.anisotropy;
                aniso.enabled = true;
                aniso.strength = parse_float(it->second, "anisotropyStrength", 0.0f);
                aniso.rotation = parse_float(it->second, "anisotropyRotation", 0.0f);
                if (it->second.IsObject())
                    fill_texture_slot_metadata_from_value(model, it->second.Get("anisotropyTexture"), aniso.texture);
            }

            if (auto it = gm.extensions.find("KHR_materials_pbrSpecularGlossiness"); it != gm.extensions.end()) {
                auto& sg = mat.pbr().extensions.pbr_specular_glossiness;
                sg.enabled = true;
                if (it->second.IsObject() && it->second.Has("diffuseFactor")) {
                    const tinygltf::Value& f = it->second.Get("diffuseFactor");
                    if (f.IsArray() && f.ArrayLen() >= 4) {
                        sg.diffuse_factor = glm::vec4(
                            (float)f.Get(0).GetNumberAsDouble(),
                            (float)f.Get(1).GetNumberAsDouble(),
                            (float)f.Get(2).GetNumberAsDouble(),
                            (float)f.Get(3).GetNumberAsDouble());
                    }
                }
                sg.specular_factor = parse_vec3(it->second, "specularFactor", glm::vec3(1.0f));
                sg.glossiness_factor = parse_float(it->second, "glossinessFactor", 1.0f);
                if (it->second.IsObject()) {
                    fill_texture_slot_metadata_from_value(model, it->second.Get("diffuseTexture"), sg.diffuse_texture);
                    fill_texture_slot_metadata_from_value(model, it->second.Get("specularGlossinessTexture"), sg.specular_glossiness_texture);
                }
            }
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

            if (materialIndex < 0 || materialIndex >= (int)model.materials.size())
                return mat;

            const tinygltf::Material& gm = model.materials[(size_t)materialIndex];
            auto& p = mat->pbr();

            if (gm.pbrMetallicRoughness.baseColorFactor.size() == 4) {
                p.base_color_factor = glm::vec4(
                    (float)gm.pbrMetallicRoughness.baseColorFactor[0],
                    (float)gm.pbrMetallicRoughness.baseColorFactor[1],
                    (float)gm.pbrMetallicRoughness.baseColorFactor[2],
                    (float)gm.pbrMetallicRoughness.baseColorFactor[3]);
            }
            p.metallic_factor = (float)gm.pbrMetallicRoughness.metallicFactor;
            p.roughness_factor = (float)gm.pbrMetallicRoughness.roughnessFactor;
            p.normal_scale = (float)gm.normalTexture.scale;
            p.occlusion_strength = (float)gm.occlusionTexture.strength;
            p.alpha_cutoff = (float)gm.alphaCutoff;
            p.double_sided = gm.doubleSided;

            if (gm.alphaMode == "MASK")
                p.alpha_mode = Material::AlphaMode::Mask;
            else if (gm.alphaMode == "BLEND")
                p.alpha_mode = Material::AlphaMode::Blend;
            else
                p.alpha_mode = Material::AlphaMode::Opaque;

            if (gm.emissiveFactor.size() == 3) {
                p.emissive_factor = glm::vec3(
                    (float)gm.emissiveFactor[0],
                    (float)gm.emissiveFactor[1],
                    (float)gm.emissiveFactor[2]);
            }

            fill_texture_slot_metadata(model, gm.pbrMetallicRoughness.baseColorTexture, p.base_color_texture);
            fill_texture_slot_metadata(model, gm.pbrMetallicRoughness.metallicRoughnessTexture, p.metallic_roughness_texture);
            fill_texture_slot_metadata(model, gm.normalTexture, p.normal_texture);
            fill_texture_slot_metadata(model, gm.occlusionTexture, p.occlusion_texture);
            fill_texture_slot_metadata(model, gm.emissiveTexture, p.emissive_texture);

            parse_material_extensions(model, gm, *mat);

            if (!options.disable_textures) {
                p.base_color_texture.texture = load_texture_from_source(
                    model, p.base_color_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                p.metallic_roughness_texture.texture = load_texture_from_source(
                    model, p.metallic_roughness_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                p.normal_texture.texture = load_texture_from_source(
                    model, p.normal_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                p.occlusion_texture.texture = load_texture_from_source(
                    model, p.occlusion_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                p.emissive_texture.texture = load_texture_from_source(
                    model, p.emissive_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);

                auto& ext = p.extensions;
                ext.clearcoat.texture.texture = load_texture_from_source(
                    model, ext.clearcoat.texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.clearcoat.roughness_texture.texture = load_texture_from_source(
                    model, ext.clearcoat.roughness_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.clearcoat.normal_texture.texture = load_texture_from_source(
                    model, ext.clearcoat.normal_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.sheen.color_texture.texture = load_texture_from_source(
                    model, ext.sheen.color_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.sheen.roughness_texture.texture = load_texture_from_source(
                    model, ext.sheen.roughness_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.specular.specular_texture.texture = load_texture_from_source(
                    model, ext.specular.specular_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.specular.specular_color_texture.texture = load_texture_from_source(
                    model, ext.specular.specular_color_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.transmission.texture.texture = load_texture_from_source(
                    model, ext.transmission.texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.volume.thickness_texture.texture = load_texture_from_source(
                    model, ext.volume.thickness_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.iridescence.texture.texture = load_texture_from_source(
                    model, ext.iridescence.texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.iridescence.thickness_texture.texture = load_texture_from_source(
                    model, ext.iridescence.thickness_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.anisotropy.texture.texture = load_texture_from_source(
                    model, ext.anisotropy.texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.pbr_specular_glossiness.diffuse_texture.texture = load_texture_from_source(
                    model, ext.pbr_specular_glossiness.diffuse_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
                ext.pbr_specular_glossiness.specular_glossiness_texture.texture = load_texture_from_source(
                    model, ext.pbr_specular_glossiness.specular_glossiness_texture.gltf_image_source, gltfDir, async, textureCacheByImageIndex, p_tex_mutex);
            }

            return mat;
        }

        static void set_default_layout_pnuv(const Ref<VertexBuffer>& vb) {
            vb->set_layout({
                { ShaderDataType::Float3, "a_position" },
                { ShaderDataType::Float3, "a_normal"   },
                { ShaderDataType::Float4, "a_tangent"  },
                { ShaderDataType::Float2, "a_uv0"      },
                { ShaderDataType::Float2, "a_uv1"      },
            });
        }

        static BufferLayout make_pnuv_layout() {
            return {
                { ShaderDataType::Float3, "a_position" },
                { ShaderDataType::Float3, "a_normal"   },
                { ShaderDataType::Float4, "a_tangent"  },
                { ShaderDataType::Float2, "a_uv0"      },
                { ShaderDataType::Float2, "a_uv1"      },
            };
        }

        static void generate_tangents(std::vector<VertexPBR>& vertices, const std::vector<uint32_t>& indices) {
            if (vertices.empty() || indices.empty())
                return;

            std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0f));
            std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0f));

            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                const uint32_t i0 = indices[i + 0];
                const uint32_t i1 = indices[i + 1];
                const uint32_t i2 = indices[i + 2];
                if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
                    continue;

                const glm::vec3& p0 = vertices[i0].position;
                const glm::vec3& p1 = vertices[i1].position;
                const glm::vec3& p2 = vertices[i2].position;

                const glm::vec2& w0 = vertices[i0].uv0;
                const glm::vec2& w1 = vertices[i1].uv0;
                const glm::vec2& w2 = vertices[i2].uv0;

                const glm::vec3 e1 = p1 - p0;
                const glm::vec3 e2 = p2 - p0;
                const glm::vec2 d1 = w1 - w0;
                const glm::vec2 d2 = w2 - w0;

                const float denom = d1.x * d2.y - d1.y * d2.x;
                if (glm::abs(denom) < 1e-8f)
                    continue;

                const float r = 1.0f / denom;
                const glm::vec3 sdir = (e1 * d2.y - e2 * d1.y) * r;
                const glm::vec3 tdir = (e2 * d1.x - e1 * d2.x) * r;

                tan1[i0] += sdir;
                tan1[i1] += sdir;
                tan1[i2] += sdir;

                tan2[i0] += tdir;
                tan2[i1] += tdir;
                tan2[i2] += tdir;
            }

            for (size_t i = 0; i < vertices.size(); ++i) {
                const glm::vec3 n = glm::normalize(vertices[i].normal);
                const glm::vec3 t = tan1[i];

                glm::vec3 tangent = t - n * glm::dot(n, t);
                if (glm::dot(tangent, tangent) < 1e-10f) {
                    tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                } else {
                    tangent = glm::normalize(tangent);
                }

                const glm::vec3 bitangent = glm::cross(n, tangent);
                const float w = (glm::dot(bitangent, tan2[i]) < 0.0f) ? -1.0f : 1.0f;
                vertices[i].tangent = glm::vec4(tangent, w);
            }
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

        struct PendingTexturePayload {
            int source = -1;
            std::shared_ptr<DecodedImageRGBA8> decoded{};
        };

        struct PendingMaterialPayload {
            Ref<Material> material;
            PendingTexturePayload base_color_texture{};
            PendingTexturePayload metallic_roughness_texture{};
            PendingTexturePayload normal_texture{};
            PendingTexturePayload occlusion_texture{};
            PendingTexturePayload emissive_texture{};
        };

        struct PendingSubmeshPayload {
            PendingMaterialPayload material{};
            std::string name;
            glm::mat4 transform{1.0f};
            std::vector<VertexPBR> vertices;
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
            GltfLoadOptions no_texture_opts = options;
            no_texture_opts.disable_textures = true;
            std::unordered_map<int, Ref<Texture2D>> dummy_cache;
            mat.material = build_material_from_gltf(
                model, materialIndex, gltfDir, no_texture_opts, dummy_cache, false, nullptr);

            if (!mat.material || options.disable_textures)
                return mat;

            auto decode_source = [&](int source, PendingTexturePayload& out_slot) {
                out_slot.source = source;
                if (source < 0 || source >= (int)model.images.size())
                    return;

                {
                    auto lk = p_tex_mutex
                        ? std::unique_lock<std::mutex>(*p_tex_mutex)
                        : std::unique_lock<std::mutex>();
                    auto it = texturePayloadCacheByImageIndex.find(source);
                    if (it != texturePayloadCacheByImageIndex.end()) {
                        out_slot.decoded = it->second;
                        return;
                    }
                }

                const tinygltf::Image& img = model.images[(size_t)source];
                auto decoded = decode_gltf_image_rgba8(img, gltfDir);
                if (!decoded || !decoded->ok()) {
                    if (decoded && !decoded->error.empty()) {
                        HN_CORE_WARN("glTF: failed to decode texture source {} ({})", source, decoded->error);
                    }
                    return;
                }

                {
                    auto lk = p_tex_mutex
                        ? std::unique_lock<std::mutex>(*p_tex_mutex)
                        : std::unique_lock<std::mutex>();
                    auto [it, inserted] = texturePayloadCacheByImageIndex.emplace(source, decoded);
                    out_slot.decoded = it->second;
                }
            };

            const auto& p = mat.material->pbr();
            decode_source(p.base_color_texture.gltf_image_source, mat.base_color_texture);
            decode_source(p.metallic_roughness_texture.gltf_image_source, mat.metallic_roughness_texture);
            decode_source(p.normal_texture.gltf_image_source, mat.normal_texture);
            decode_source(p.occlusion_texture.gltf_image_source, mat.occlusion_texture);
            decode_source(p.emissive_texture.gltf_image_source, mat.emissive_texture);

            PendingTexturePayload ext_decode_tmp{};
            decode_source(p.extensions.clearcoat.texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.clearcoat.roughness_texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.clearcoat.normal_texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.sheen.color_texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.sheen.roughness_texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.specular.specular_texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.specular.specular_color_texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.transmission.texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.volume.thickness_texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.iridescence.texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.iridescence.thickness_texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.anisotropy.texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.pbr_specular_glossiness.diffuse_texture.gltf_image_source, ext_decode_tmp);
            decode_source(p.extensions.pbr_specular_glossiness.specular_glossiness_texture.gltf_image_source, ext_decode_tmp);
            return mat;
        }

        struct ImportedPrimitivePayload {
            std::string name;
            std::vector<VertexPBR> vertices;
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
            const tinygltf::Accessor* uv1Acc = nullptr;
            if (auto it = prim.attributes.find("TEXCOORD_1"); it != prim.attributes.end())
                uv1Acc = find_accessor(model, it->second);
            const tinygltf::Accessor* tanAcc = nullptr;
            if (auto it = prim.attributes.find("TANGENT"); it != prim.attributes.end())
                tanAcc = find_accessor(model, it->second);

            std::vector<VertexPBR> vertices(vcount);

            for (size_t i = 0; i < vcount; ++i) {
                if (!read_vec3_float(model, *posAcc, i, vertices[i].position))
                    return std::nullopt;

                if (nrmAcc)
                    read_vec3_float(model, *nrmAcc, i, vertices[i].normal);

                if (uvAcc)
                    read_vec2_float(model, *uvAcc, i, vertices[i].uv0);
                if (uv1Acc)
                    read_vec2_float(model, *uv1Acc, i, vertices[i].uv1);
                if (tanAcc)
                    read_vec4_float(model, *tanAcc, i, vertices[i].tangent);
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

            if (!tanAcc)
                generate_tangents(vertices, indices);

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
            const tinygltf::Accessor* uv1Acc = nullptr;
            if (auto it = prim.attributes.find("TEXCOORD_1"); it != prim.attributes.end())
                uv1Acc = find_accessor(model, it->second);
            const tinygltf::Accessor* tanAcc = nullptr;
            if (auto it = prim.attributes.find("TANGENT"); it != prim.attributes.end())
                tanAcc = find_accessor(model, it->second);

            std::vector<VertexPBR> vertices((size_t)posAcc->count);
            for (size_t i = 0; i < vertices.size(); ++i) {
                if (!read_vec3_float(model, *posAcc, i, vertices[i].position))
                    return std::nullopt;
                if (nrmAcc)
                    read_vec3_float(model, *nrmAcc, i, vertices[i].normal);
                if (uvAcc)
                    read_vec2_float(model, *uvAcc, i, vertices[i].uv0);
                if (uv1Acc)
                    read_vec2_float(model, *uv1Acc, i, vertices[i].uv1);
                if (tanAcc)
                    read_vec4_float(model, *tanAcc, i, vertices[i].tangent);
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

            if (!tanAcc)
                generate_tangents(vertices, indices);

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
            std::vector<VertexPBR>      opt_vertices;
            std::vector<uint32_t>        opt_indices;
            std::vector<meshopt_Meshlet> meshlets;
            std::vector<uint32_t>        meshlet_vertices;
            std::vector<uint8_t>         meshlet_triangles;
            std::vector<MeshletBounds>   bounds;
        };

        static std::optional<MeshletBuildResult> build_meshlet_geometry(
              const std::vector<VertexPBR>& vertices,
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
            std::vector<VertexPBR> opt_vertices = vertices;

            const size_t vertex_stride = sizeof(VertexPBR);

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
                        (uint32_t)(result->opt_vertices.size() * sizeof(VertexPBR)));
                    vb->set_data(result->opt_vertices.data(),
                                 (uint32_t)(result->opt_vertices.size() * sizeof(VertexPBR)));
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
                        (uint32_t)(primData.vertices.size() * sizeof(VertexPBR)));
                    vb->set_data(primData.vertices.data(),
                                 (uint32_t)(primData.vertices.size() * sizeof(VertexPBR)));
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
                std::vector<uint32_t>        global_flat_indices;

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
                        v_floats, v_floats + mb.opt_vertices.size() * k_vertex_floats);
                    vertex_cursor += (uint32_t)mb.opt_vertices.size();

                    for (const auto& raw_m : mb.meshlets) {
                        // raw_m.vertex_offset and raw_m.triangle_offset are LOCAL to mb arrays
                        for (uint32_t t = 0; t < raw_m.triangle_count; t++) {
                            for (uint32_t v = 0; v < 3; v++) {
                                uint8_t  local_idx  = mb.meshlet_triangles[raw_m.triangle_offset + t * 3 + v];
                                uint32_t global_idx = mb.meshlet_vertices[raw_m.vertex_offset + local_idx] + v_off;
                                global_flat_indices.push_back(global_idx);
                            }
                        }

                        // then do the adjusted push for the rasterizer path
                        meshopt_Meshlet adjusted = raw_m;
                        adjusted.vertex_offset   += mv_off;
                        adjusted.triangle_offset += mt_off;
                        global_meshlets.push_back(adjusted);
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
                    global_vertices, StorageBufferUsage::Immutable | StorageBufferUsage::RTGeometry);
                global_bufs.meshlets_buffer = StorageBuffer::create_from_vector(
                    global_meshlets, StorageBufferUsage::Immutable);
                global_bufs.meshlet_vertices_buffer = StorageBuffer::create_from_vector(
                    global_meshlet_vertices, StorageBufferUsage::Immutable);
                global_bufs.meshlet_triangles_buffer = StorageBuffer::create_from_vector(
                    global_meshlet_triangles, StorageBufferUsage::Immutable);
                global_bufs.meshlet_bounds_buffer = StorageBuffer::create_from_vector(
                    global_bounds, StorageBufferUsage::Immutable);
                global_bufs.flat_index_buffer = StorageBuffer::create_from_vector(
                    global_flat_indices, StorageBufferUsage::Immutable | StorageBufferUsage::RTGeometry);
                global_bufs.flat_index_count = (uint32_t)global_flat_indices.size();

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
                        v_floats, v_floats + build.submesh.vertices.size() * k_vertex_floats);
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
            Ref<Material> mat = payload.material ? payload.material : Material::create();
            auto& p = mat->pbr();

            auto resolve_texture = [&](const PendingTexturePayload& src, Ref<Texture2D>& dst) {
                if (src.source < 0 || !src.decoded)
                    return;

                auto it = textureCacheByImageIndex.find(src.source);
                if (it == textureCacheByImageIndex.end()) {
                    Ref<Texture2D> tex = finalize_texture_payload(src.decoded);
                    if (tex) {
                        it = textureCacheByImageIndex.emplace(src.source, tex).first;
                    }
                }
                if (it != textureCacheByImageIndex.end())
                    dst = it->second;
            };

            resolve_texture(payload.base_color_texture, p.base_color_texture.texture);
            resolve_texture(payload.metallic_roughness_texture, p.metallic_roughness_texture.texture);
            resolve_texture(payload.normal_texture, p.normal_texture.texture);
            resolve_texture(payload.occlusion_texture, p.occlusion_texture.texture);
            resolve_texture(payload.emissive_texture, p.emissive_texture.texture);

            auto resolve_slot_from_source = [&](Material::TextureSlot& slot) {
                if (slot.gltf_image_source < 0)
                    return;
                auto it = textureCacheByImageIndex.find(slot.gltf_image_source);
                if (it != textureCacheByImageIndex.end()) {
                    slot.texture = it->second;
                }
            };

            auto& ext = p.extensions;
            resolve_slot_from_source(ext.clearcoat.texture);
            resolve_slot_from_source(ext.clearcoat.roughness_texture);
            resolve_slot_from_source(ext.clearcoat.normal_texture);
            resolve_slot_from_source(ext.sheen.color_texture);
            resolve_slot_from_source(ext.sheen.roughness_texture);
            resolve_slot_from_source(ext.specular.specular_texture);
            resolve_slot_from_source(ext.specular.specular_color_texture);
            resolve_slot_from_source(ext.transmission.texture);
            resolve_slot_from_source(ext.volume.thickness_texture);
            resolve_slot_from_source(ext.iridescence.texture);
            resolve_slot_from_source(ext.iridescence.thickness_texture);
            resolve_slot_from_source(ext.anisotropy.texture);
            resolve_slot_from_source(ext.pbr_specular_glossiness.diffuse_texture);
            resolve_slot_from_source(ext.pbr_specular_glossiness.specular_glossiness_texture);

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
                    static_cast<uint32_t>(submeshPayload.vertices.size() * sizeof(VertexPBR)));
                vb->set_data(submeshPayload.vertices.data(),
                             static_cast<uint32_t>(submeshPayload.vertices.size() * sizeof(VertexPBR)));
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
                const auto& mb = *payload.meshlet_buffers;
                std::vector<uint32_t> flat_indices;
                flat_indices.reserve(mb.meshlets.size() * 64 * 3);
                for (const auto& m : mb.meshlets) {
                    for (uint32_t t = 0; t < m.triangle_count; t++) {
                        for (uint32_t v = 0; v < 3; v++) {
                            uint8_t  local_idx  = mb.meshlet_triangles[m.triangle_offset + t * 3 + v];
                            uint32_t global_idx = mb.meshlet_vertices[m.vertex_offset + local_idx];
                            flat_indices.push_back(global_idx);
                        }
                    }
                }

                GlobalMeshletBuffers global_bufs{};
                global_bufs.vertex_buffer = StorageBuffer::create_from_vector(
                    mb.vertices, StorageBufferUsage::Immutable | StorageBufferUsage::RTGeometry);
                global_bufs.meshlets_buffer = StorageBuffer::create_from_vector(
                    mb.meshlets, StorageBufferUsage::Immutable);
                global_bufs.meshlet_vertices_buffer = StorageBuffer::create_from_vector(
                    mb.meshlet_vertices, StorageBufferUsage::Immutable);
                global_bufs.meshlet_triangles_buffer = StorageBuffer::create_from_vector(
                    mb.meshlet_triangles, StorageBufferUsage::Immutable);
                global_bufs.meshlet_bounds_buffer = StorageBuffer::create_from_vector(
                    mb.bounds, StorageBufferUsage::Immutable);
                global_bufs.flat_index_buffer = StorageBuffer::create_from_vector(
                    flat_indices, StorageBufferUsage::Immutable | StorageBufferUsage::RTGeometry);
                global_bufs.flat_index_count = (uint32_t)flat_indices.size();
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
