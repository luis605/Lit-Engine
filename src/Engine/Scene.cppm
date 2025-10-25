export module scene;

import std;
import model;
import shader;

export class Scene {
  public:
    Scene();

    void addModel(Model&& model);
    void draw(Shader& shader) const;

    std::vector<Model>& getModels();

  private:
    std::vector<Model> m_models;
};
