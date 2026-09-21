module;

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>

export module Engine.glm;

export namespace glm {
using ::glm::mat4;
using ::glm::inverse;
using ::glm::vec4;
using ::glm::vec3;
using ::glm::vec2;
using ::glm::ivec2;
using ::glm::ivec3;
using ::glm::ortho;
using ::glm::lookAt;
using ::glm::distance;
using ::glm::distance2;
using ::glm::sqrt;
using ::glm::normalize;
using ::glm::perspective;
using ::glm::perspectiveRH_ZO;
using ::glm::radians;
using ::glm::degrees;
using ::glm::rotate;
using ::glm::scale;
using ::glm::quat;
using ::glm::abs;
using ::glm::max;
using ::glm::min;
using ::glm::translate;
using ::glm::value_ptr;
using ::glm::sin;
using ::glm::cos;
using ::glm::cross;
using ::glm::length;
using ::glm::dot;
using ::glm::cos;
using ::glm::sin;
using ::glm::clamp;
using ::glm::angleAxis;
using ::glm::mat4_cast;
using ::glm::slerp;
using ::glm::mat3;
using ::glm::mat3_cast;
using ::glm::quat_cast;

using ::glm::operator*;
using ::glm::operator+;
using ::glm::operator-;
using ::glm::operator/;
} // namespace glm