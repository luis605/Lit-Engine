#include <iostream>
#include <string>
#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#include <tbb/tbb.h>

namespace py = pybind11;

int main() {
    py::scoped_interpreter guard{}; // Initialize Python interpreter
    
    // Define the Python script as a string
    std::string script = "import time\nfor i in range(10):\n    time.sleep(5)\n    print('Hi')";
    
    // Define a lambda function that executes the script
    auto execute_script = [&script]() {
        py::exec(script);
    };
    
    // Create a TBB task group
    tbb::task_group tg;
    
    // Submit the script execution task to TBB
    tg.run(execute_script);
    
    // Continue executing C++ code
    for (int i = 0; i < 10; i++) {
        std::cout << "Hello" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // Wait for the script execution task to complete
    tg.wait();
    
    return 0;
}
