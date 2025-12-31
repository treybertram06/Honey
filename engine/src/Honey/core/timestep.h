#pragma once

namespace Honey {

    class Timestep {

    public:
        Timestep(float time = 0.0f)
            : m_time(time) {

        }

        operator float() const { return m_time; }

        float get_seconds() const { return m_time; }
        float get_millis() const { return m_time * 1000.0f; }

    private:
        float m_time;
    };
}
