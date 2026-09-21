module;

#include <cstdint>

module Sandbox.scene;

import Engine.Render.component;
import Engine.glm;

Entity Scene::spawn(uint32_t mesh, const glm::vec3& position, const glm::vec3& scale, Entity parent) {
    Entity e = m_ctx.scene.createEntity();
    m_ctx.scene.transforms[e].localMatrix = glm::scale(glm::translate(glm::mat4(1.0f), position), scale);
    m_ctx.scene.renderables[e].mesh_uuid = mesh;
    m_ctx.scene.renderables[e].material_uuid = 0;
    m_ctx.scene.renderables[e].shaderId = 0;
    m_ctx.scene.hierarchies[e].parent = parent;
    m_ctx.scene.markHierarchyDirty();
    m_ctx.scene.markDataDirty();
    return e;
}

void Scene::setLocal(Entity e, const glm::mat4& local) {
    m_ctx.scene.transforms[e].localMatrix = local;
    m_ctx.scene.markTransformsDirty();
}

void Scene::onStart() {
    const glm::vec3 size(0.5f, 1.5f, 0.5f);
    const float gap = 0.2f;

    m_lower = spawn(m_ctx.cubeMesh, glm::vec3(0.0f), size);
    m_upper = spawn(m_ctx.cubeMesh, glm::vec3(0.0f, size.y + gap, 0.0f), size);

    m_ctx.camera.setPos(glm::vec3(1.5f, 0.6f, 5.0f));
}

void Scene::onUpdate(float deltaTime, float time) {
}
