#include "hnpch.h"
#include "math.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace Honey::Math {

    bool decompose_transform(const glm::mat4& transform, glm::vec3& out_translation, glm::vec3& out_rotation, glm::vec3& out_scale) {

        // from glm::decompose in matrix_decompose.inl

        using namespace glm;
        using T = float;

        mat4 LocalMatrix(transform);

        if (epsilonEqual(LocalMatrix[3][3], static_cast<T>(0.0), epsilon<T>()))
            return false;

        if (
            epsilonNotEqual(LocalMatrix[0][3], static_cast<T>(0.0), epsilon<T>()) ||
            epsilonNotEqual(LocalMatrix[1][3], static_cast<T>(0.0), epsilon<T>()) ||
            epsilonNotEqual(LocalMatrix[2][3], static_cast<T>(0.0), epsilon<T>())) {

            LocalMatrix[0][3] = LocalMatrix[1][3] = LocalMatrix[2][3] = static_cast<T>(0.0);
            LocalMatrix[3][3] = static_cast<T>(1);
        }

        out_translation = vec3(LocalMatrix[3]);
        LocalMatrix[3] = vec4(0.0, 0.0, 0.0, LocalMatrix[3].w);

        vec3 Row[3], Pdum3;

        for (length_t i = 0; i < 3; ++i)
            for (length_t j = 0; j < 3; ++j)
                Row[i][j] = LocalMatrix[i][j];

        out_scale.x = length(Row[0]);
        Row[0] = detail::scale(Row[0], static_cast<T>(1));
        out_scale.y = length(Row[1]);
        Row[1] = detail::scale(Row[1], static_cast<T>(1));
        out_scale.z = length(Row[2]);
        Row[2] = detail::scale(Row[2], static_cast<T>(1));

#if 0
    Pdum3 = cross(Row[1], Row[2]);
        if (dot(Row[0], Pdum3) < 0) {
            for (length_t i = 0; i < 3; ++i) {
                out_scale[i] *= static_cast<T>(-1);
                Row[i] *= static_cast<T>(-1);
            }
        }
#endif

        out_rotation.y = asin(-Row[0][2]);
        if (cos(out_rotation.y) != 0) {
            out_rotation.x = atan2(Row[1][2], Row[2][2]);
            out_rotation.z = atan2(Row[0][1], Row[0][0]);
        } else {
            out_rotation.x = atan2(-Row[2][0], Row[1][1]);
            out_rotation.z = 0;
        }

        return true;

    }

}
