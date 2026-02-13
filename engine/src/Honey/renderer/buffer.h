#pragma once

namespace Honey {

    enum class ShaderDataType {
        None = 0, Float, Float2, Float3, Float4, Mat3, Mat4, Int, Int2, Int3, Int4, Bool
    };

    static uint32_t shader_data_type_size(ShaderDataType type) {

        switch (type) {
            case ShaderDataType::Float:     return 4;
            case ShaderDataType::Float2:    return 4*2;
            case ShaderDataType::Float3:    return 4*3;
            case ShaderDataType::Float4:    return 4*4;
            case ShaderDataType::Mat3:      return 4*3*3;
            case ShaderDataType::Mat4:      return 4*4*4;
            case ShaderDataType::Int:       return 4;
            case ShaderDataType::Int2:      return 4*2;
            case ShaderDataType::Int3:      return 4*3;
            case ShaderDataType::Int4:      return 4*4;
            case ShaderDataType::Bool:      return 1;
            case ShaderDataType::None:      return 0;

        }

        HN_CORE_ASSERT(false, "Unknown ShaderDataType!");
        return 0;
    }

    struct BufferElement {
        std::string name;
        ShaderDataType type;
        std::size_t offset;
        uint32_t size;
        bool normalized;
        bool instanced;

        BufferElement() {}

        BufferElement(ShaderDataType t,
                  const std::string& n,
                  bool norm     = false,
                  bool inst     = false)        // ← NEW
        : name(n), type(t), size(shader_data_type_size(t)),
          offset(0), normalized(norm), instanced(inst) {}

        uint32_t get_component_count() const {

            switch (type) {

                case ShaderDataType::Float:     return 1;
                case ShaderDataType::Float2:    return 2;
                case ShaderDataType::Float3:    return 3;
                case ShaderDataType::Float4:    return 4;
                case ShaderDataType::Mat3:      return 3*3;
                case ShaderDataType::Mat4:      return 4*4;
                case ShaderDataType::Int:       return 1;
                case ShaderDataType::Int2:      return 2;
                case ShaderDataType::Int3:      return 3;
                case ShaderDataType::Int4:      return 4;
                case ShaderDataType::Bool:      return 1;
                case ShaderDataType::None:      return 0;
            }
            HN_CORE_ASSERT(false, "Unknown ShaderDataType!");
            return 0;
        }
    };

    class BufferLayout {
    public:
        BufferLayout() {}

        BufferLayout(const std::initializer_list<BufferElement>& elements)
            : m_elements(elements) {
            calculate_offsets_and_stride();
        }

        inline uint32_t get_stride() const { return m_stride; }
        inline const std::vector<BufferElement>& get_elements() const { return m_elements; }

        std::vector<BufferElement>::iterator begin() { return m_elements.begin(); }
        std::vector<BufferElement>::iterator end() { return m_elements.end(); }
        std::vector<BufferElement>::const_iterator begin() const { return m_elements.begin(); }
        std::vector<BufferElement>::const_iterator end() const { return m_elements.end(); }

    private:

        void calculate_offsets_and_stride() {
            uint32_t offset = 0;
            m_stride = 0;
            for (auto& element : m_elements) {
                element.offset = offset;
                offset += element.size;
                m_stride += element.size;
            }
        }

        std::vector<BufferElement> m_elements;
        uint32_t m_stride = 0;
    };

    class VertexBuffer {
    public:
        virtual ~VertexBuffer() {}

        virtual void bind() const = 0;
        virtual void unbind() const = 0;

        virtual void set_data(const void* data, uint32_t size) = 0;

        virtual void set_layout(const BufferLayout& layout) = 0;
        virtual const BufferLayout& get_layout() const = 0;

        static Ref<VertexBuffer> create(uint32_t size);
        static Ref<VertexBuffer> create(float* vertices, uint32_t size);

    };

    class IndexBuffer {
    public:
        virtual ~IndexBuffer() {};

        virtual void bind() const = 0;
        virtual void unbind() const = 0;

        virtual uint32_t get_count() const = 0;

        static Ref<IndexBuffer> create(uint32_t* indices, uint32_t count);
        static Ref<IndexBuffer> create(uint16_t* indices, uint32_t count);

    };

    class UniformBuffer {
    public:
        virtual ~UniformBuffer() {}

        virtual void bind() const = 0;
        virtual void unbind() const = 0;

        virtual void set_data(uint32_t size, const void* data) = 0;

        static Ref<UniformBuffer> create(uint32_t size, uint32_t binding);
    };
}