
export module engine;

import renderer;
export class Engine {
  public:
    Engine();
    ~Engine();

    void init();
    void update();

  private:
    Renderer m_renderer;
};