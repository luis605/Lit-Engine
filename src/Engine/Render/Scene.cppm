export module Engine.scene;

import Engine.model;
import Engine.shader;
import std;

export class Scene {
  public:
    Scene();

    void addModel(Model&& model);
    void draw(Shader& shader) const;

    std::vector<Model>& getModels();

  private:
    std::vector<Model> m_models;
};
