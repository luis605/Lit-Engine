module;
#include <print>

module application;

Application::Application() {
    std::println("Application created");
}

Application::~Application() {
    std::println("Application destroyed");
}

void Application::greet() const {
    std::println("Hello from Application");
}