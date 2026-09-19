module;

#include <cstdint>
#include <string_view>
#include <vector>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ISwapChain;
} // namespace Diligent

export module Engine.UI.manager;

import Engine.glm;

export class UIManager {
  public:
    UIManager();
    ~UIManager();

    void init(Diligent::IRenderDevice* pDevice, Diligent::IDeviceContext* pContext, Diligent::ISwapChain* pSwapChain, const int windowWidth, const int windowHeight);
    void cleanup();

    // The characters are copied into a persistent, reused arena, so callers may
    // pass temporaries (the view does not have to outlive the call).
    void addText(std::string_view text, float x, float y, float scale, const glm::vec3& color);
    void render();

  private:
    unsigned int m_windowWidth = 0;
    unsigned int m_windowHeight = 0;

    struct TextData {
        std::uint32_t offset;
        std::uint32_t length;
        float x, y, scale;
        glm::vec3 color;
    };

    // Both containers keep their capacity across frames: steady state is
    // allocation free.
    std::vector<TextData> m_texts;
    std::vector<char> m_textArena;

    void* m_diligent = nullptr;
};
