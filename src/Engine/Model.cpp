module model;

import std;
import mesh;
import shader;
import glm;

Model::Model(std::vector<Mesh>&& meshes)
    : m_meshes(std::move(meshes)), m_transform(glm::mat4(1.0f)) {}

void Model::draw(Shader& shader) const {
    shader.setUniform("model", m_transform);
    for (const auto& mesh : m_meshes) {
        mesh.draw();
    }
}

glm::mat4 Model::getTransform() const {
    return m_transform;
}

void Model::setTransform(const glm::mat4& transform) {
    m_transform = transform;
}
