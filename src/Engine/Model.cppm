export module model;

import std;
import mesh;
import shader;
import glm;

export class Model {
  public:
    Model(std::vector<Mesh>&& meshes);

    void draw(Shader& shader) const;

    glm::mat4 getTransform() const;
    void setTransform(const glm::mat4& transform);

  private:
    std::vector<Mesh> m_meshes;
    glm::mat4 m_transform;
};
