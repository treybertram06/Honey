#pragma once
#include "Honey/renderer/shader.h"
#import <Metal/Metal.h>

namespace Honey {

class MetalShader : public Shader {
public:
    MetalShader(id<MTLDevice> device, const std::string& path);
    MetalShader(id<MTLDevice> device,
                const std::string& name,
                const std::string& vertex_src,
                const std::string& fragment_src);

    /* Shader overrides -----------------------------------------------------*/
    void bind()   const override {}   // global bind not needed
    void unbind() const override {}

    void set_float   (const std::string&, float)               override;
    void set_float2  (const std::string&, const glm::vec2&)    override;
    void set_float3  (const std::string&, const glm::vec3&)    override;
    void set_float4  (const std::string&, const glm::vec4&)    override;
    void set_mat4    (const std::string&, const glm::mat4&)    override;
    void set_int     (const std::string&, int)                 override;
    void set_int_array(const std::string&, int*, std::uint32_t)     override;

    const std::string& get_name() const override { return m_name; }

    /* Metal helpers --------------------------------------------------------*/
    void apply(id<MTLRenderCommandEncoder> enc) const; // bind pipeline+UBO
    id<MTLRenderPipelineState> pipeline() const { return m_pso; }

private:
    // — helpers —
    void compile(const std::string& vs, const std::string& fs);
    void upload(const std::string& uniform, const void* data, size_t sz);

    std::string read_file(const std::string& path);
    std::unordered_map<std::string,std::string>
        preprocess(const std::string& src);                    // #type splitter

    id<MTLDevice>                 m_dev = nil;
    id<MTLRenderPipelineState>    m_pso = nil;
    id<MTLBuffer>                 m_ubo = nil;   // grow‑as‑needed CPU→GPU copy
    std::unordered_map<std::string,size_t> m_offsets; // name → byte offset
    size_t                       m_head = 0;     // current used bytes
    std::string                  m_name;
};

} // namespace Honey
