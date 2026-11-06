module;

#include <cstdint>
#include <functional>

export module Engine.GalleryScene;

import Engine.engine;
import Engine.glm;
import Engine.Render.material;

export struct GalleryKit {
    uint32_t cube = 0;
    uint32_t sphere = 0;
    std::function<uint32_t(const Material&)> material;
    bool colliders = false;
};

export struct GallerySpawn {
    glm::vec3 feet{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

export void buildGalleryScene(Engine& engine, const GalleryKit& kit);
export [[nodiscard]] GallerySpawn gallerySpawn();
export void enableGalleryPlay(Engine& engine, const GalleryKit& kit);
