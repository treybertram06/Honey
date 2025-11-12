#pragma once
#include <cstdint>

namespace Honey {

    class UUID {
    public:
        UUID();
        UUID(uint64_t uuid);
        UUID(const UUID&) = default;

        operator uint64_t() const { return m_uuid; }

    private:
        uint64_t m_uuid;
    };
}

namespace std {

    template<> // huh wuh huh
    struct hash<Honey::UUID> {
        std::size_t operator()(const Honey::UUID& uuid) const {
            return hash<uint64_t>()(uuid);
        }
    };
}
