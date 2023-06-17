#include "../include_all.h"

#define MAX(a, b) ((a)>(b)? (a) : (b))
#define MIN(a, b) ((a)<(b)? (a) : (b))

void CleanUp() {
    UnloadShader(shader);


    in_game_preview = false;

    std::cout << "Stuck 1\n";
    
    for (Entity &entity : entities_list)
        entity.running = false;

    std::cout << "Stuck 2\n";
    

    for (auto& script_thread : scripts_thread_vector) {
        if (script_thread.joinable())
            script_thread.join();
    }

    std::cout << "Stuck 3\n";
    
    scripts_thread_vector.clear();

    std::cout << "Stuck 4\n";
    

    for (Entity &entity : entities_list)
        entity.remove();

}
