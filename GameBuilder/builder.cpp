
void BuildProject()
{
    string directoryName = "exported_game";
    if (!std::filesystem::exists(directoryName)) {
        if (std::filesystem::create_directory(directoryName)) {
            std::cout << "Directory created successfully." << std::endl;
        } else {
            std::cerr << "Failed to create directory." << std::endl;
            return 1;
        }
    } else {
        std::cout << "Directory already exists." << std::endl;
    }

    const char* compileCommand = R"""(
    cp project.json exported_game && cp -r project/ exported_game && cd GameBuilder && make build 'STRING1="$$(cat ../Engine/Lighting/shaders/lighting_vertex.glsl)"' 'STRING2="$$(cat ../Engine/Lighting/shaders/lighting_fragment.glsl)"' 
    )""";

    int result = system(compileCommand);

    if (result == 0) {
        std::cout << "Game Sucessfully Exported" << std::endl;
        return 0;
    } else {
        std::cout << "ERROR: Game Could Not Be Exported due to errors while building the executable!" << std::endl;
        return 1;
    }
}