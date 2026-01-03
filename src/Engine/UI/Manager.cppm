module;

#include <vector>
#include <memory>
#include <string>
#include <map>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ISwapChain;
} // namespace Diligent

import Engine.glm;

export module Engine.UI.manager;

export class UIManager {
  public:
    UIManager();
    ~UIManager();

    void init(Diligent::IRenderDevice* pDevice, Diligent::IDeviceContext* pContext, Diligent::ISwapChain* pSwapChain, const int windowWidth, const int windowHeight);
    void cleanup();

    void addText(const std::string& text, float x, float y, float scale, const glm::vec3& color);
    void render();

  private:
    unsigned int m_windowWidth;
    unsigned int m_windowHeight;

    struct TextData {
        std::string text;
        float x, y, scale;
        glm::vec3 color;
    };
    std::vector<TextData> m_texts;

    void* m_diligent = nullptr;
};
