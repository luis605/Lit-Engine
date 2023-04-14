#include <iostream>
#include <future>
#include <pybind11/embed.h>

namespace py = pybind11;

bool loop1 = true;
bool loop2 = true;

void myFunction(int x) {
    std::cout << "myFunction called with " << x << std::endl;
    loop1 = false;
}

void loop() {
    while (true) {
        std::cout << "Loop 1=============" << std::endl;
    }
}

void loop2Task() {
    while (loop2) {
        std::cout << "Loop 2####################" << std::endl;
    }
}

class Script {
public:
    Script(const std::string& script) : script_(script) {}

    void run() {

        while (true) {
            auto future = std::async(std::launch::async, [this]() {
                py::exec(script_);
            });

            future.wait();
        }
    }

private:
    std::string script_;
};

int main() {
    py::scoped_interpreter interpreter{};

    Script script(R"(
        while True:
            print("Sleeping...")
    )");

    std::future<void> future = std::async(std::launch::async, &Script::run, &script);
    // std::future<void> future1 = std::async(std::launch::async, myFunction, 42);
    // std::future<void> future2 = std::async(std::launch::async, loop);
    // std::future<void> future3 = std::async(std::launch::async, loop2Task);

    script.run(); // Call the run function to execute the Python script

    return 0;
}
