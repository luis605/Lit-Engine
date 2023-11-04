/*
#include "include_all.h"

int LitEngine()
{
    Startup();
    EngineMainLoop();
    CleanUp();

    return 0;
}


*/

#include <iostream>

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

//    LitEngine();
    std::cout << "Hello World" << std::endl;
    return 0;
}