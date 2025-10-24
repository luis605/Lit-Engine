
import engine;
import renderer;

Engine::Engine() {}

Engine::~Engine() {}

void Engine::init() { m_renderer.init(); }

void Engine::update() { m_renderer.drawScene(); }