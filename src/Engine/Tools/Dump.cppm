module;

#include <cstdint>
#include <string>
#include <string_view>

export module Engine.Dump;

import Engine.World;
import Engine.Json;
import Engine.Rng;

export struct DumpOptions {
    bool components = true;
    bool scripts = true;
    bool worldTransforms = true;
};

export Json dumpWorld(const World& world, const DumpOptions& options = {});
export Json dumpRandom(const RandomService& random);
export uint64_t hashText(std::string_view text);
export std::string hashHex(uint64_t hash);
