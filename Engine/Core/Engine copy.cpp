class Entity {
private:
    string script_content;

public:
    Entity(const Entity& other) {
        if (!this || this == nullptr || !other.initialized)
            return;

        this->script_content = other.script_content;

    }


    Entity& operator=(const Entity& other) {
        if (this == &other) {
            return *this;  // Handle self-assignment
        }

        this->script_content = other.script_content;

        return *this;
    }


    void setupScript(LitCamera* rendering_camera)
    {
        if (script.empty() && script_index.empty()) return;

        if (script_content.empty())
        {
            std::ifstream infile("scripts.json");
            if (!infile.is_open()) {
                std::cout << "Error: Failed to open scripts file." << std::endl;
                return 1;
            }

            json json_data;
            infile >> json_data;

            infile.close();

            if (json_data.is_array() && !json_data.empty()) {
                json first_element = json_data.at(0);

                if (first_element.is_object()) {
                    if (first_element.contains("coins collector0")) {
                        script_content = first_element["coins collector0"].get<std::string>();
                        std::cout << "Script loaded successfully." << std::endl;
                        std::cout << script_content << std::endl;
                    } else {
                        std::cout << "Key 'coins collector0' not found in the first element." << std::endl;
                    }
                } else {
                    std::cout << "First element is not an object." << std::endl;
                    return;
                }
            } else {
                std::cout << "JSON data is not an array or is empty." << std::endl;
                return;
            }
        }

        try {
            script_module = py::module("__main__");

            for (auto item : locals) {
                script_module.attr(item.first) = item.second;
            }
            
            py::eval<py::eval_statements>(script_content, script_module.attr("__dict__"));
        } catch (const py::error_already_set& e) {
            py::print(e.what());
        }

    }


    void runScript(LitCamera* rendering_camera) {
        if (script.empty() && script_index.empty()) return;


        if (script_content.empty())
        {
            std::ifstream infile("scripts.json");
            if (!infile.is_open()) {
                std::cout << "Error: Failed to open scripts file." << std::endl;
                return 1;
            }

            json json_data;
            infile >> json_data;

            infile.close();

            if (json_data.is_array() && !json_data.empty()) {
                json first_element = json_data.at(0);

                if (first_element.is_object()) {
                    if (first_element.contains("coins collector0")) {
                        script_content = first_element["coins collector0"].get<std::string>();
                        std::cout << "Script loaded successfully." << std::endl;
                        std::cout << script_content << std::endl;
                    } else {
                        std::cout << "Key 'coins collector0' not found in the first element." << std::endl;
                    }
                } else {
                    std::cout << "First element is not an object." << std::endl;
                    return;
                }
            } else {
                std::cout << "JSON data is not an array or is empty." << std::endl;
                return;
            }
        }


        try {
            if (script_module.attr("__dict__").contains("update")) {
                py::object update_func = script_module.attr("update");

                locals["time"] = py::cast(&time_instance);
                rendering_camera->update();
                update_func();
                last_frame_count = time_instance.dt;
            }
        } catch (const py::error_already_set& e) {
            std::cerr << "Error running script: " << e.what() << std::endl;
        }
    }

}
