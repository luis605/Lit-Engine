#include "../include_all.h"

#define MAX(a, b) ((a)>(b)? (a) : (b))
#define MIN(a, b) ((a)<(b)? (a) : (b))

void CleanUp() {
    UnloadShader(shader);
}