module;

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ISwapChain;
struct ITextureView;
enum TEXTURE_FORMAT : std::uint16_t;
}  // namespace Diligent

export module Engine.UI.imgui;

export class ImGuiLayer {
  public:
    ImGuiLayer();
    ~ImGuiLayer();
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    bool init(GLFWwindow* window, Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::ISwapChain* swapChain, Diligent::TEXTURE_FORMAT depthFormat);
    void shutdown();
    void newFrame();
    void render();
    [[nodiscard]] bool frameActive() const { return m_frameActive; }
    [[nodiscard]] bool initialized() const { return m_data != nullptr; }

  private:
    struct Data;
    Data* m_data = nullptr;
    bool m_frameActive = false;
};
