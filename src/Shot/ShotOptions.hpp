#pragma once

#include <string>
#include <utility>
#include <vector>

constexpr int kDefaultFrames = 30;
constexpr float kDefaultLightIntensity = 3000000.0f;
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 720;

struct ShotOptions {
    std::string testScene = "vista";
    std::string sceneFile;
    std::string output = "shot.png";
    std::string timingsFile;
    std::vector<std::pair<std::string, std::string>> settings;
    float lightIntensity = kDefaultLightIntensity;
    bool hasCamera = false;
    float cameraValues[5] = {};
    bool noProbe = false;
    int bakeFrame = 0;
    int frames = kDefaultFrames;
    int width = kDefaultWidth;
    int height = kDefaultHeight;
};

void printUsage();
bool parseArguments(int argc, char** argv, ShotOptions& options);
