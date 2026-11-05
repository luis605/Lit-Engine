module;

#include "RendererPrelude.hpp"

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.mesh;
import Engine.Render.lighting;
import Engine.Render.custom;
import Engine.Render.settings;
import Engine.Render.material;
import Engine.Portal.view;
import Engine.Render.observation;

#include "RendererInternal.hpp"

namespace {

using FunctionChunk = std::pair<std::string, std::string>;

bool IsIdentifierChar(char character) { return std::isalnum(static_cast<unsigned char>(character)) || character == '_'; }

bool DefinesFunction(const std::string& chunk, const char* name) {
    size_t position = 0;
    while ((position = chunk.find(name, position)) != std::string::npos) {
        const bool leftOk = position == 0 || !IsIdentifierChar(chunk[position - 1]);
        size_t after = position + std::strlen(name);
        while (after < chunk.size() && std::isspace(static_cast<unsigned char>(chunk[after]))) ++after;
        size_t before = position;
        while (before > 0 && std::isspace(static_cast<unsigned char>(chunk[before - 1]))) --before;
        const bool isVoid = before >= 4 && chunk.compare(before - 4, 4, "void") == 0;
        if (leftOk && isVoid && after < chunk.size() && chunk[after] == '(' && chunk.find('{', after) != std::string::npos) return true;
        position += 1;
    }
    return false;
}

std::string ExtractPragmas(const std::string& text, SurfaceSource& out) {
    std::string body;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string directive, group, option;
        words >> directive >> group >> option;
        if (directive != "#pragma" || group != "lit") {
            body += line + "\n";
            continue;
        }
        if (option == "bounds") {
            float value = 0.0f;
            if (words >> value) out.bounds = std::max(value, 0.0f);
        } else if (option == "twosided") {
            out.twoSided = true;
        }
        body += "\n";
    }
    return body;
}

bool StartsPreprocessorLine(const std::string& body, size_t newlineIndex) {
    size_t lineStart = body.rfind('\n', newlineIndex == 0 ? 0 : newlineIndex - 1);
    lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
    const size_t first = body.find_first_not_of(" \t", lineStart);
    return first != std::string::npos && first < newlineIndex && body[first] == '#';
}

void SplitTopLevelChunks(const std::string& body, SurfaceSource& out) {
    size_t start = 0;
    int depth = 0;
    bool lineComment = false;
    bool blockComment = false;
    auto located = [&](size_t from, size_t end) { return litgpu::userLine(static_cast<int>(std::count(body.begin(), body.begin() + static_cast<std::ptrdiff_t>(from), '\n')) + 1) + body.substr(from, end - from); };
    auto emit = [&](size_t end) {
        std::string chunk = located(start, end);
        start = end;
        if (DefinesFunction(chunk, "vertex")) {
            out.vertexCode += chunk;
            out.hasVertex = true;
        } else if (DefinesFunction(chunk, "surface")) {
            out.fragmentCode += chunk;
            out.hasSurface = true;
        } else {
            out.shared += chunk;
        }
    };
    for (size_t index = 0; index < body.size(); ++index) {
        const char current = body[index];
        const char next = index + 1 < body.size() ? body[index + 1] : '\0';
        if (lineComment) {
            if (current == '\n') lineComment = false;
            continue;
        }
        if (blockComment) {
            if (current == '*' && next == '/') {
                blockComment = false;
                ++index;
            }
            continue;
        }
        if (current == '/' && next == '/') {
            lineComment = true;
            continue;
        }
        if (current == '/' && next == '*') {
            blockComment = true;
            continue;
        }
        if (current == '{') ++depth;
        if (current == '}') {
            --depth;
            if (depth == 0) emit(index + 1);
        }
        if (current == ';' && depth == 0) emit(index + 1);
        if (current == '\n' && depth == 0 && StartsPreprocessorLine(body, index)) emit(index + 1);
    }
    if (start < body.size()) out.shared += located(start, body.size());
}

std::string FunctionName(const std::string& chunk, size_t parenthesis) {
    size_t nameEnd = parenthesis;
    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(chunk[nameEnd - 1]))) --nameEnd;
    size_t nameStart = nameEnd;
    while (nameStart > 0 && IsIdentifierChar(chunk[nameStart - 1])) --nameStart;
    return chunk.substr(nameStart, nameEnd - nameStart);
}

void SplitSharedChunks(const std::string& shared, std::vector<FunctionChunk>& functions, std::vector<std::string>& declarations) {
    int braceDepth = 0;
    size_t chunkStart = 0;
    for (size_t index = 0; index < shared.size(); ++index) {
        if (shared[index] == '{') ++braceDepth;
        if (shared[index] == '}' && --braceDepth == 0) {
            const std::string chunk = shared.substr(chunkStart, index + 1 - chunkStart);
            const size_t parenthesis = chunk.find('(');
            const size_t brace = chunk.find('{');
            if (parenthesis != std::string::npos && parenthesis < brace) {
                functions.emplace_back(FunctionName(chunk, parenthesis), chunk);
            } else {
                declarations.push_back(chunk);
            }
            chunkStart = index + 1;
        }
        if (shared[index] == ';' && braceDepth == 0) {
            declarations.push_back(shared.substr(chunkStart, index + 1 - chunkStart));
            chunkStart = index + 1;
        }
    }
    if (chunkStart < shared.size()) declarations.push_back(shared.substr(chunkStart));
}

std::vector<bool> FindReachableFunctions(const std::string& vertexCode, const std::vector<FunctionChunk>& functions) {
    std::vector<bool> used(functions.size(), false);
    std::string reachable = vertexCode;
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t functionIndex = 0; functionIndex < functions.size(); ++functionIndex) {
            const std::string& name = functions[functionIndex].first;
            if (used[functionIndex] || !DefinesFunction(" void " + name + "(x){", name.c_str())) continue;
            if (reachable.find(name + "(") == std::string::npos && reachable.find(name + " (") == std::string::npos) continue;
            used[functionIndex] = true;
            reachable += functions[functionIndex].second;
            changed = true;
        }
    }
    return used;
}

}

SurfaceSource ParseSurface(const std::string& text) {
    SurfaceSource out;
    const std::string body = ExtractPragmas(text, out);
    SplitTopLevelChunks(body, out);

    std::vector<FunctionChunk> functions;
    std::vector<std::string> declarations;
    SplitSharedChunks(out.shared, functions, declarations);
    const std::vector<bool> used = FindReachableFunctions(out.vertexCode, functions);

    for (const std::string& declaration : declarations) out.vertexShared += declaration;
    for (size_t functionIndex = 0; functionIndex < functions.size(); ++functionIndex) {
        if (used[functionIndex]) out.vertexShared += functions[functionIndex].second;
    }
    return out;
}
