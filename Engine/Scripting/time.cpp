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
