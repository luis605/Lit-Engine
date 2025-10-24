
import engine;
import renderer;
import camera;

Engine::Engine() {}

Engine::~Engine() {}

void Engine::init() { m_renderer.init(); }

void Engine::update(Camera& camera) { m_renderer.drawScene(camera); }