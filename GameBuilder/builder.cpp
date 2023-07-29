
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

    const char* compileCommand = "cp project.json exported_game && cp -r project/ exported_game && cd GameBuilder && make build";

    int result = system(compileCommand);

    if (result == 0) {
        std::cout << "Game Sucessfully Exported" << std::endl;
        return 0;
    } else {
        std::cout << "Game NOT Exported due to errors!" << std::endl;
        return 1;
    }
}