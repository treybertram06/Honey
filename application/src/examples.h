#include <Honey.h>

class PongLayer : public Honey::Layer {
public:
    PongLayer()
        : Layer("Pong"),
        m_camera(2.0f, (16.0f / 9.0f), -1.0f, 1.0f),
        m_camera_position(0.0f, 0.0f, 0.0f),
        paddle_pos(-1.5f, 0.0f, 0.0f) {

        m_vertex_array.reset(Honey::VertexArray::create());

        float vertices[3*3 + 4*3] = {
            -0.5f, -0.5f, 0.0f,     1.0f, 0.0f, 1.0f, 1.0f,
             0.5f, -0.5f, 0.0f,     0.0f, 0.0f, 1.0f, 1.0f,
             0.0f,  0.5f, 0.0f,     1.0f, 1.0f, 0.0f, 1.0f
        };

        std::shared_ptr<Honey::VertexBuffer> vertex_buffer;
        vertex_buffer.reset(Honey::VertexBuffer::create(vertices, sizeof(vertices)));


        Honey::BufferLayout layout = {
            { Honey::ShaderDataType::Float3, "a_pos" },
            { Honey::ShaderDataType::Float4, "a_color" }
        };
        vertex_buffer->set_layout(layout);
        m_vertex_array->add_vertex_buffer(vertex_buffer);


        unsigned int indices[3] = { 0, 1, 2 };
        std::shared_ptr<Honey::IndexBuffer> index_buffer;
        index_buffer.reset(Honey::IndexBuffer::create(indices, 3));
        m_vertex_array->set_index_buffer(index_buffer);

        m_square_vertex_array.reset(Honey::VertexArray::create());

        float vertices_sq[3*4] = {
            -0.5f, -0.5f, 0.0f,
             0.5f, -0.5f, 0.0f,
             0.5f,  0.5f, 0.0f,
            -0.5f,  0.5f, 0.0f,
        };

        std::shared_ptr<Honey::VertexBuffer> square_vertex_buffer;
        square_vertex_buffer.reset(Honey::VertexBuffer::create(vertices_sq, sizeof(vertices_sq)));
        Honey::BufferLayout square_layout = {
            { Honey::ShaderDataType::Float3, "a_pos" },
        };
        square_vertex_buffer->set_layout(square_layout);
        m_square_vertex_array->add_vertex_buffer(square_vertex_buffer);

        unsigned int square_indices[6] = { 0, 1, 2, 2, 3, 0 };
        std::shared_ptr<Honey::IndexBuffer> square_index_buffer;
        square_index_buffer.reset(Honey::IndexBuffer::create(square_indices, sizeof(square_indices)));
        m_square_vertex_array->set_index_buffer(square_index_buffer);

        std::string vertex_src = R"(

            #version 330 core

            layout(location = 0) in vec3 a_pos;
            layout(location = 1) in vec4 a_color;

            uniform mat4 u_view_projection;
            uniform mat4 u_transform;


            out vec3 v_pos;
            out vec4 v_color;

            void main() {
                v_pos = a_pos;
                v_color = a_color;
                gl_Position = u_view_projection * u_transform * vec4(a_pos, 1.0);
            }
        )";

        std::string fragment_src = R"(

            #version 330 core

            layout(location = 0) out vec4 color;

            in vec3 v_pos;
            in vec4 v_color;

            void main() {
                color = vec4(v_pos * 0.5 + 0.5, 1.0);
                color = v_color;
            }
        )";

        m_shader = Honey::Shader::create("basic_shader", vertex_src, fragment_src);




        std::string blue_shader_vertex_src = R"(

            #version 330 core

            layout(location = 0) in vec3 a_pos;

            uniform mat4 u_view_projection;
            uniform mat4 u_transform;

            out vec3 v_pos;

            void main() {
                v_pos = a_pos;
                gl_Position = u_view_projection * u_transform * vec4(a_pos, 1.0);
            }
        )";

        std::string blue_shader_fragment_src = R"(

            #version 330 core

            layout(location = 0) out vec4 color;

            in vec3 v_pos;

            void main() {
                color = vec4(0.3, 0.3, 0.8, 1.0);
            }
        )";

        m_blue_shader = Honey::Shader::create("ball_shader", blue_shader_vertex_src, blue_shader_fragment_src);
    }

    void on_update(Honey::Timestep ts) override {

        //HN_TRACE("Deltatime: {0}s ({1}ms)", ts.get_seconds(), ts.get_millis());

        if (Honey::Input::is_key_pressed(HN_KEY_W))
            paddle_pos.y += m_camera_speed * ts;
        if (Honey::Input::is_key_pressed(HN_KEY_S))
            paddle_pos.y -= m_camera_speed * ts;

        if (Honey::Input::is_key_pressed(HN_KEY_I))
            bad_paddle_pos.y += m_camera_speed * ts;
        if (Honey::Input::is_key_pressed(HN_KEY_K))
            bad_paddle_pos.y -= m_camera_speed * ts;





        ball_pos.x += ball_vel.x * ts;
        ball_pos.y += ball_vel.y * ts;

        //HN_TRACE("Ball_pos.y: {0}", ball_pos.y);
        const float epsilon = 0.001f;
        if (ball_pos.y <= -1.0f) {
            ball_pos.y = -1.0f;
            ball_vel.y = -ball_vel.y;
        }
        else if (ball_pos.y >= 1.0f) {
            ball_pos.y = 1.0f;
            ball_vel.y = -ball_vel.y;
        }

        if (ball_pos.x >= 1.45f - epsilon &&
            ball_pos.x <= 1.55f + epsilon &&
            ball_pos.y <= bad_paddle_pos.y + 0.5f &&
            ball_pos.y >= bad_paddle_pos.y - 0.5f) {

            ball_vel.y += ball_pos.y - bad_paddle_pos.y;

            ball_pos.x = 1.45;
            ball_vel.x = -ball_vel.x * 1.1f;
        }

        if (ball_pos.x <= -1.45f + epsilon &&
            ball_pos.x >= -1.55f - epsilon &&
            ball_pos.y <= paddle_pos.y + 0.5f &&
            ball_pos.y >= paddle_pos.y - 0.5f) {

            ball_vel.y += ball_pos.y - paddle_pos.y;

            ball_pos.x = -1.45f;
            ball_vel.x = -ball_vel.x * 1.1f;
        }

        if (ball_pos.x <= -1.55f) {
            right_score++;
            ball_pos = {-1.0f, 0.0f, 0.0f};
            ball_vel = {1.0f, 0.0f, 0.0f};
        }
        if (ball_pos.x >= 1.55f) {
            left_score++;
            ball_pos = {-1.0f, 0.0f, 0.0f};
            ball_vel = {-1.0f, 0.0f, 0.0f};
        }





        Honey::RenderCommand::set_clear_color({0.1f, 0.1f, 0.1f, 1.0f});
        Honey::RenderCommand::clear();

        m_camera.set_position(m_camera_position);
        m_camera.set_rotation(m_camera_rotation);

        Honey::Renderer::begin_scene(m_camera);

        glm::mat4 paddle_scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 1.0f, 1.0f));
        glm::mat4 ball_scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 1.0f));

        glm::mat4 paddle_transform = glm::translate(glm::mat4(1.0f), paddle_pos) * paddle_scale;
        glm::mat4 bad_paddle_transform = glm::translate(glm::mat4(1.0f), bad_paddle_pos) * paddle_scale;
        glm::mat4 ball_transform = glm::translate(glm::mat4(1.0f), ball_pos) * ball_scale;




        Honey::Renderer::submit(m_blue_shader, m_square_vertex_array, paddle_transform);
        Honey::Renderer::submit(m_blue_shader, m_square_vertex_array, bad_paddle_transform);
        Honey::Renderer::submit(m_blue_shader, m_square_vertex_array, ball_transform);


        //Honey::Renderer::submit(m_shader, m_vertex_array);

        Honey::RenderCommand::draw_indexed(m_square_vertex_array);
        Honey::RenderCommand::draw_indexed(m_vertex_array);

        Honey::Renderer::end_scene();

    }

    virtual void on_imgui_render() override {
        ImGui::Begin("Left Score");
        ImGui::Text("Left Score: %d", left_score);
        ImGui::End();

        ImGui::Begin("Right Score");
        ImGui::Text("Right Score: %d", right_score);
        ImGui::End();


    }

    void on_event(Honey::Event &event) override {

        Honey::EventDispatcher dispatcher(event);
        dispatcher.dispatch<Honey::KeyPressedEvent>(HN_BIND_EVENT_FN(PongLayer::on_key_pressed_event));

    }

    bool on_key_pressed_event(Honey::KeyPressedEvent& event) {

        if (event.get_key_code() == HN_KEY_ESCAPE)
            Honey::Application::quit();

        return false;
    }

private:
    std::shared_ptr<Honey::Shader> m_shader;
    std::shared_ptr<Honey::VertexArray> m_vertex_array;

    std::shared_ptr<Honey::Shader> m_blue_shader;
    std::shared_ptr<Honey::VertexArray> m_square_vertex_array;

    Honey::OrthographicCamera m_camera;
    glm::vec3 m_camera_position;
    float m_camera_rotation;

    glm::vec3 paddle_pos;
    glm::vec3 ball_pos = {-1.0f, 0.0f, 0.0f};
    glm::vec3 ball_vel = {1.0f, 0.0f, 0.0f};
    glm::vec3 bad_paddle_pos = {1.5f, 0.0f, 0.0f};
    int left_score = 0;
    int right_score = 0;

    float m_camera_speed = 1.0f;
    float m_camera_rotation_speed = 60.0f; //      degrees / second
};