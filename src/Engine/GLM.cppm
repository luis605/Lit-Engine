module;

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

export module glm;

export namespace glm {
using ::glm::mat4;
using ::glm::vec3;
using ::glm::lookAt;
using ::glm::normalize;
using ::glm::perspective;
using ::glm::radians;
using ::glm::rotate;
using ::glm::scale;
using ::glm::translate;
using ::glm::value_ptr;
using ::glm::sin;
using ::glm::cos;
using ::glm::cross;

using ::glm::operator*;
using ::glm::operator+;
using ::glm::operator-;
using ::glm::operator/;
} // namespace glm