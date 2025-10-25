module Engine.scene;

import std;
import Engine.model;
import Engine.shader;

Scene::Scene() {}

void Scene::addModel(Model&& model) {
    m_models.emplace_back(std::move(model));
}

void Scene::draw(Shader& shader) const {
    for (const auto& model : m_models) {
        model.draw(shader);
    }
}

std::vector<Model>& Scene::getModels() {
    return m_models;
}
