#ifndef TIME_H
#define TIME_H

class Time {
public:
    float dt;

public:
    void update();
};

inline void UpdateInGameGlobals();

Time timeInstance;

#endif // TIME_H