#include "include_all.h"

int LitEngine()
{
    Startup();
    std::cout << "Lit Engine Started" << std::endl << std::endl << std::endl << std::endl;
    EngineMainLoop();
    CleanUp();

    return 0;
}


int main()
{
/*
    // Create a pipe
    if (pipe(pipe_fds) == -1) {
        std::cerr << "Pipe creation failed." << std::endl;
        return 1;
    }


    pid = fork();
    if (pid < 0) {
        std::cerr << "Failed to start Lit Engine - Fork failed." << std::endl;
    } else if (pid == 0) {
        setpgid(0, 0);
        std::cout << "This is the child process." << std::endl;
        PreviewProject();
    } else {
        std::cout << "This is the parent process." << std::endl;
        LitEngine();
    }
*/

    LitEngine();
    return 0;
}