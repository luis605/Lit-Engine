/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef TIME_H
#define TIME_H

class Time {
public:
    float dt;

public:
    void update();
};

inline void UpdateInGameGlobals();

extern Time timeInstance;

#endif // TIME_H