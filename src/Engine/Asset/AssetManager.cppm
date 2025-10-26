module;
#include <string>
#include <vector>
#include <iostream>
#include <optional>
import Engine.mesh;

export module Engine.asset;

export namespace AssetManager {
bool bake(const std::string& sourcePath, const std::string& destinationPath);
std::optional<Mesh> load(const std::string& assetPath);
} // namespace AssetManager
