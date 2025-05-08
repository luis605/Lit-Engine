/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <raylib.h>

class Time {
public:
    float dt;

    void update() {
        dt = GetFrameTime();
    }
};
Time timeInstance;

void UpdateInGameGlobals() {
    timeInstance.update();
}
