#import "platform/metal/metal_shader.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>

#include "Honey/core/core.h"
#include "Honey/core/log.h"
#include <glfw/glfw3.h>

namespace fs = std::filesystem;
namespace Honey {

// ---------------------------------------------------------------------------
//  File helpers
// ---------------------------------------------------------------------------
static std::string slurp(const std::string& p)
{
    std::ifstream file(fs::absolute(p));
    HN_CORE_ASSERT(file.is_open(), "Can't open shader %s", p.c_str());
    std::stringstream ss;  ss << file.rdbuf();  return ss.str();
}

static std::unordered_map<std::string,std::string>
split_by_type(const std::string& src)
{
    const char* tag = "#type";
    std::unordered_map<std::string,std::string> out;
    size_t pos = src.find(tag);
    while (pos != std::string::npos) {
        size_t eol = src.find_first_of("\r\n", pos);
        size_t begin = pos + strlen(tag) + 1;
        std::string type = src.substr(begin, eol-begin);

        size_t nextLine = src.find_first_not_of("\r\n", eol);
        pos = src.find(tag, nextLine);
        out[type] = src.substr(nextLine,
                      pos == std::string::npos ? std::string::npos
                                                : pos - nextLine);
    }
    return out;
}

// ---------------------------------------------------------------------------
//  CTORs
// ---------------------------------------------------------------------------
MetalShader::MetalShader(id<MTLDevice> dev, const std::string& path)
    : m_dev(dev)
{
    const auto src  = slurp(path);
    const auto parts= split_by_type(src);

    compile(parts.at("vertex"), parts.at("fragment"));

    auto namePos = path.find_last_of("/\\");
    auto dotPos  = path.rfind('.');
    m_name = path.substr(namePos==std::string::npos?0:namePos+1,
                         dotPos==std::string::npos?path.size():dotPos-namePos-1);
}

MetalShader::MetalShader(id<MTLDevice> dev, const std::string& n,
                         const std::string& vs, const std::string& fs)
    : m_dev(dev), m_name(n)
{ compile(vs, fs); }

// ---------------------------------------------------------------------------
//  Compilation   (runtime – convenient for parity with OpenGL path)
// ---------------------------------------------------------------------------
void MetalShader::compile(const std::string& vs, const std::string& fs)
{
    std::string combined = vs + "\n" + fs;
    NSError* err = nil;
    id<MTLLibrary> lib =
        [m_dev newLibraryWithSource:@(combined.c_str())
                            options:nil
                              error:&err];                         // :contentReference[oaicite:2]{index=2}
    HN_CORE_ASSERT(lib, "MSL compile failed: %s",
                   err.localizedDescription.UTF8String);

    auto vFunc = [lib newFunctionWithName:@"vertex_main"];
    auto fFunc = [lib newFunctionWithName:@"fragment_main"];

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction   = vFunc;
    desc.fragmentFunction = fFunc;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;

    m_pso = [m_dev newRenderPipelineStateWithDescriptor:desc error:&err];
    HN_CORE_ASSERT(m_pso, "Pipeline creation failed: %s",
                   err.localizedDescription.UTF8String);

    // cheap uniform buffer: start with 4 KiB, grow if needed
    m_ubo = [m_dev newBufferWithLength:4096 options:MTLResourceStorageModeShared];
}

// ---------------------------------------------------------------------------
//  Uniform setters – linear allocator inside m_ubo --------------------------
// ---------------------------------------------------------------------------
void MetalShader::upload(const std::string& n, const void* data, size_t sz)
{
    auto it = m_offsets.find(n);
    size_t off;
    if (it == m_offsets.end()) {
        // allocate a new slice
        off = m_head;
        m_offsets[n] = off;
        m_head += sz;
        if (m_head > m_ubo.length) {          // grow
            size_t newLen = std::max<size_t>(m_head, m_ubo.length*2);
            m_ubo = [m_dev newBufferWithLength:newLen
                                        options:MTLResourceStorageModeShared];
        }
    } else off = it->second;

    std::memcpy(static_cast<uint8_t*>(m_ubo.contents)+off, data, sz);
}

#define ULOAD(name,v) upload(name, &(v), sizeof(v))

void MetalShader::set_float (const std::string& n, float v)         { ULOAD(n,v);}
void MetalShader::set_int   (const std::string& n, int v)           { ULOAD(n,v);}
void MetalShader::set_float2(const std::string& n,const glm::vec2&v){ ULOAD(n,v);}
void MetalShader::set_float3(const std::string& n,const glm::vec3&v){ ULOAD(n,v);}
void MetalShader::set_float4(const std::string& n,const glm::vec4&v){ ULOAD(n,v);}
void MetalShader::set_mat4  (const std::string& n,const glm::mat4&v){ ULOAD(n,v);}

void MetalShader::set_int_array(const std::string& n,int* v,std::uint32_t c)
{ upload(n, v, sizeof(int)*c); }

// ---------------------------------------------------------------------------
//  Apply: bind pipeline + UBO each draw‑call group --------------------------
// ---------------------------------------------------------------------------
void MetalShader::apply(id<MTLRenderCommandEncoder> enc) const
{
    [enc setRenderPipelineState:m_pso];
    [enc setVertexBuffer:m_ubo offset:0 atIndex:1];  // binding 1 in both stages
    [enc setFragmentBuffer:m_ubo offset:0 atIndex:1];
}

} // namespace Honey
