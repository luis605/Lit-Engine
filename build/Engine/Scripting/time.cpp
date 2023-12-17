class Time {
public:
    float dt;

    void update() {
        dt = GetFrameTime();
    }
};


Time time_instance;

void UpdateInGameGlobals()
{
    time_instance.update();
}
