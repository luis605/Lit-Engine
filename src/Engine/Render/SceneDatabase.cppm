export module Engine.Render.scenedatabase;

import Engine.Render.entity;
import Engine.Render.component;
import std;

export class SceneDatabase {
public:
    std::vector<TransformComponent> transforms;
    std::vector<HierarchyComponent> hierarchies;
    std::vector<RenderableComponent> renderables;

    Entity createEntity() {
        transforms.emplace_back();
        hierarchies.emplace_back();
        renderables.emplace_back();
        return static_cast<Entity>(transforms.size() - 1);
    }
};
