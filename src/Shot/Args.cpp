#include "ShotOptions.hpp"

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {

bool parseSize(const std::string& text, ShotOptions& options) {
    const size_t separator = text.find('x');
    if (separator == std::string::npos) return false;
    options.width = std::atoi(text.substr(0, separator).c_str());
    options.height = std::atoi(text.substr(separator + 1).c_str());
    return options.width > 0 && options.height > 0;
}

}

void printUsage() {
    std::puts(
        "LitShot: render N frames of a scene on a real device and save the final frame as PNG\n"
        "  --testscene NAME      test scene (portal_torture, streaming_route, crowd, vista, showcase, probes, gallery)\n"
        "  --scene FILE          load a .litscene instead\n"
        "  --frames N            frames to render before the capture (default 30)\n"
        "  --light-intensity X   intensity of the showcase scene lights (default 3e6, photometric lumens)\n"
        "  --camera X,Y,Z,YAW,PITCH   override the camera (showcase default 0,3,12,-90,-8; 0,2.5,6,-60,-6 frames walls and outdoor apron)\n"
        "  --no-probe            probes scene: omit the reflection probe entity\n"
        "  --bake-frame N        request a reflection probe bake on frame N\n"
        "  --size WxH            window size (default 1280x720)\n"
        "  --set KEY=VALUE       render setting, repeatable (reflectionMode=2, renderScale=0.5, ...)\n"
        "  --screenshot FILE     output PNG (default shot.png)\n"
        "  --timings FILE        write GPU pass timings (ms) of the last frame\n");
}

bool parseArguments(int argc, char** argv, ShotOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        auto nextValue = [&]() -> std::string { return index + 1 < argc ? argv[++index] : ""; };
        if (argument == "--testscene") {
            options.testScene = nextValue();
        } else if (argument == "--scene") {
            options.sceneFile = nextValue();
        } else if (argument == "--frames") {
            options.frames = std::atoi(nextValue().c_str());
        } else if (argument == "--light-intensity") {
            options.lightIntensity = static_cast<float>(std::atof(nextValue().c_str()));
        } else if (argument == "--camera") {
            const std::string text = nextValue();
            float* values = options.cameraValues;
            options.hasCamera = std::sscanf(text.c_str(), "%f,%f,%f,%f,%f", &values[0], &values[1], &values[2], &values[3], &values[4]) == 5;
            if (!options.hasCamera) return false;
        } else if (argument == "--no-probe") {
            options.noProbe = true;
        } else if (argument == "--bake-frame") {
            options.bakeFrame = std::atoi(nextValue().c_str());
        } else if (argument == "--size") {
            if (!parseSize(nextValue(), options)) return false;
        } else if (argument == "--screenshot") {
            options.output = nextValue();
        } else if (argument == "--timings") {
            options.timingsFile = nextValue();
        } else if (argument == "--set") {
            const std::string pair = nextValue();
            const size_t equals = pair.find('=');
            if (equals == std::string::npos) return false;
            options.settings.emplace_back(pair.substr(0, equals), pair.substr(equals + 1));
        } else {
            return false;
        }
    }
    return options.frames > 0;
}
