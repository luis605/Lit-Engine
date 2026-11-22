#include "TestSupport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <fstream>
#include <string>
#include <vector>

import Engine.glm;
import Engine.mesh;
import Engine.Skin;
import Engine.SkinRig;
import Engine.Rig;
import Engine.Face;
import Engine.asset;

namespace {

bool nearly(float a, float b, float eps = 1.0e-4f) { return std::fabs(a - b) <= eps; }

glm::vec3 position(const std::vector<float>& v, size_t i) { return glm::vec3(v[i * kVertexFloats], v[i * kVertexFloats + 1], v[i * kVertexFloats + 2]); }
glm::vec3 normal(const std::vector<float>& v, size_t i) { return glm::vec3(v[i * kVertexFloats + 3], v[i * kVertexFloats + 4], v[i * kVertexFloats + 5]); }

bool insideSphere(const glm::vec4& sphere, const glm::vec3& p) { return glm::length(p - glm::vec3(sphere)) <= sphere.w + 1.0e-4f; }

void testWeights() {
    std::vector<SkinVertex> skin(4);
    skin[0].weights = {2.0f, 2.0f, 0.0f, 0.0f};
    skin[0].joints = {0, 1, 0, 0};
    skin[1].weights = {-1.0f, 0.0f, 0.0f, 0.0f};
    skin[2].weights = {0.5f, 0.5f, 0.0f, 0.0f};
    skin[2].joints = {1, 99, 0, 0};
    skin[3].weights = {std::nanf(""), 0.25f, 0.25f, 0.25f};
    skin[3].joints = {0, 1, 2, 1};
    const size_t repaired = normalizeSkinWeights(skin, 3);
    CHECK(repaired == 4);
    for (const SkinVertex& vertex : skin) {
        float sum = 0.0f;
        for (size_t k = 0; k < 4; ++k) {
            CHECK(vertex.weights[k] >= 0.0f);
            CHECK(vertex.joints[k] < 3);
            sum += vertex.weights[k];
        }
        CHECK(nearly(sum, 1.0f));
    }
    CHECK(nearly(skin[0].weights[0], 0.5f));
    CHECK(skin[1].weights[0] == 1.0f && skin[1].joints[0] == 0);
    CHECK(nearly(skin[2].weights[0], 1.0f));
    CHECK(nearly(skin[3].weights[1], 1.0f / 3.0f));
}

void testValidate() {
    DynamicMesh mesh = makeProceduralCharacter();
    std::string error;
    CHECK(validateDynamicMesh(mesh, &error));
    mesh.skin.pop_back();
    CHECK(!validateDynamicMesh(mesh, &error));
    CHECK(!error.empty());
    DynamicMesh empty;
    CHECK(!validateDynamicMesh(empty, nullptr));
    DynamicMesh bad = makeProceduralCharacter();
    bad.morphs[0].positionDelta.pop_back();
    CHECK(!validateDynamicMesh(bad, &error));
    DynamicMesh badIndex = makeProceduralCharacter();
    badIndex.rest.indices[0] = static_cast<unsigned int>(badIndex.vertexCount() + 5);
    CHECK(!validateDynamicMesh(badIndex, &error));
}

void testCharacter() {
    const DynamicMesh mesh = makeProceduralCharacter();
    CHECK(mesh.joints.size() == 17);
    CHECK(mesh.skinned());
    CHECK(mesh.vertexCount() > 500);
    CHECK(mesh.rest.indices.size() % 3 == 0);
    CHECK(mesh.findJoint("elbowR") >= 0 && mesh.findJoint("nope") < 0);
    CHECK(mesh.findMorph("jawOpen") >= 0 && mesh.findMorph("eyeBlinkLeft") >= 0);
    for (const SkinVertex& vertex : mesh.skin) {
        float sum = 0.0f;
        for (size_t k = 0; k < 4; ++k) {
            sum += vertex.weights[k];
            CHECK(vertex.joints[k] < mesh.joints.size());
        }
        CHECK(nearly(sum, 1.0f));
    }
    // Inverse bind of the root is the inverse of its bind translation.
    const glm::vec4 hips = mesh.joints[0].inverseBind * glm::vec4(mesh.joints[0].position, 1.0f);
    CHECK(nearly(hips.x, 0.0f) && nearly(hips.y, 0.0f) && nearly(hips.z, 0.0f));
}

void testWinding() {
    const DynamicMesh mesh = makeProceduralCharacter();
    size_t inward = 0;
    for (size_t i = 0; i + 2 < mesh.rest.indices.size(); i += 3) {
        const unsigned int a = mesh.rest.indices[i], b = mesh.rest.indices[i + 1], c = mesh.rest.indices[i + 2];
        const glm::vec3 geometric = glm::cross(position(mesh.rest.vertices, b) - position(mesh.rest.vertices, a), position(mesh.rest.vertices, c) - position(mesh.rest.vertices, a));
        const glm::vec3 shading = normal(mesh.rest.vertices, a) + normal(mesh.rest.vertices, b) + normal(mesh.rest.vertices, c);
        if (glm::dot(geometric, shading) < -1.0e-6f) ++inward;
    }
    std::printf("inward triangles %zu of %zu\n", inward, mesh.rest.indices.size() / 3);
    CHECK(inward == 0);
}

void testPaletteAndBindPose() {
    const DynamicMesh mesh = makeProceduralCharacter();
    const Skeleton skeleton = skeletonFromSkin(mesh);
    CHECK(skeleton.size() == mesh.joints.size());
    std::vector<glm::mat4> palette;
    computeSkinPalette(skeleton, Pose::bind(skeleton), mesh, palette);
    CHECK(palette.size() == mesh.joints.size());
    for (const glm::mat4& matrix : palette) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) CHECK(nearly(matrix[c][r], c == r ? 1.0f : 0.0f, 1.0e-4f));
        }
    }
    std::vector<float> out;
    DeformSettings none;
    deformMeshCpu(mesh, palette, {}, none, out);
    for (size_t i = 0; i < mesh.vertexCount(); i += 17) CHECK(glm::length(position(out, i) - position(mesh.rest.vertices, i)) < 1.0e-4f);
}

void testRigidTranslation() {
    const DynamicMesh mesh = makeProceduralCharacter();
    std::vector<glm::mat4> palette(mesh.joints.size(), glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)));
    std::vector<float> out;
    DeformSettings none;
    deformMeshCpu(mesh, palette, {}, none, out);
    for (size_t i = 0; i < mesh.vertexCount(); i += 13) {
        CHECK(glm::length(position(out, i) - position(mesh.rest.vertices, i) - glm::vec3(1.0f, 2.0f, 3.0f)) < 1.0e-4f);
        CHECK(glm::length(normal(out, i) - normal(mesh.rest.vertices, i)) < 1.0e-4f);
    }
}

void testElbowBend() {
    const DynamicMesh mesh = makeProceduralCharacter();
    const Skeleton skeleton = skeletonFromSkin(mesh);
    Pose pose = Pose::bind(skeleton);
    const int elbow = skeleton.find("elbowL");
    CHECK(elbow >= 0);
    pose.joints[static_cast<size_t>(elbow)].rotation = glm::angleAxis(1.2f, glm::vec3(1.0f, 0.0f, 0.0f));
    std::vector<glm::mat4> palette;
    computeSkinPalette(skeleton, pose, mesh, palette);
    std::vector<float> out;
    DeformSettings none;
    deformMeshCpu(mesh, palette, {}, none, out);
    // Vertices bound only to the left wrist must move; vertices bound to the right leg must not.
    const int wrist = mesh.findJoint("wristL");
    const int kneeR = mesh.findJoint("kneeR");
    float wristMove = 0.0f;
    float legMove = 0.0f;
    for (size_t i = 0; i < mesh.vertexCount(); ++i) {
        const float moved = glm::length(position(out, i) - position(mesh.rest.vertices, i));
        if (mesh.skin[i].joints[0] == static_cast<uint32_t>(wrist) && mesh.skin[i].weights[0] > 0.99f) wristMove = std::max(wristMove, moved);
        if (mesh.skin[i].joints[0] == static_cast<uint32_t>(kneeR) && mesh.skin[i].weights[0] > 0.99f) legMove = std::max(legMove, moved);
    }
    CHECK(wristMove > 0.1f);
    CHECK(legMove < 1.0e-4f);
}

void testMorphAndBounds() {
    const DynamicMesh mesh = makeProceduralCharacter();
    const Skeleton skeleton = skeletonFromSkin(mesh);
    const SkinBounds bounds = buildSkinBounds(mesh);
    const int jaw = mesh.findMorph("jawOpen");
    std::vector<float> weights(mesh.morphs.size(), 0.0f);
    weights[static_cast<size_t>(jaw)] = 1.0f;
    std::vector<glm::mat4> palette;
    computeSkinPalette(skeleton, Pose::bind(skeleton), mesh, palette);
    std::vector<float> rest;
    std::vector<float> opened;
    DeformSettings none;
    deformMeshCpu(mesh, palette, {}, none, rest);
    deformMeshCpu(mesh, palette, weights, none, opened);
    float lowestRest = 1.0e9f;
    float lowestOpen = 1.0e9f;
    for (size_t i = 0; i < mesh.vertexCount(); ++i) {
        lowestRest = std::min(lowestRest, position(rest, i).y);
        if (mesh.skin[i].joints[0] == 4) lowestOpen = std::min(lowestOpen, position(opened, i).y);
    }
    float headRestLow = 1.0e9f;
    for (size_t i = 0; i < mesh.vertexCount(); ++i) {
        if (mesh.skin[i].joints[0] == 4) headRestLow = std::min(headRestLow, position(rest, i).y);
    }
    CHECK(lowestOpen < headRestLow - 0.01f);

    // Conservative bounds: every deformed vertex, over several animated poses, morphs and deform modes, lies inside.
    SkeletalLibrary library;
    const uint32_t walk = library.add(makeWalkClip(skeleton));
    const uint32_t wave = library.add(makeWaveClip(skeleton));
    Pose pose;
    for (const uint32_t clipId : {walk, wave}) {
        for (int step = 0; step < 12; ++step) {
            sampleSkeletalClip(skeleton, *library.get(clipId), 0.13f * static_cast<float>(step), pose);
            computeSkinPalette(skeleton, pose, mesh, palette);
            for (const float morph : {0.0f, 1.0f}) {
                std::fill(weights.begin(), weights.end(), morph);
                for (const DeformMode mode : {DeformMode::None, DeformMode::Wave, DeformMode::Cloth}) {
                    DeformSettings deform;
                    deform.mode = mode;
                    deform.time = 0.3f * static_cast<float>(step);
                    deform.amplitude = 0.07f;
                    deform.pinY = 1.6f;
                    deform.pinRange = 1.0f;
                    deform.recomputeNormals = false;
                    std::vector<float> out;
                    deformMeshCpu(mesh, palette, weights, deform, out);
                    const glm::vec4 sphere = computeDynamicBounds(bounds, palette, weights, deform);
                    bool all = true;
                    for (size_t i = 0; i < mesh.vertexCount(); ++i) all = all && insideSphere(sphere, position(out, i));
                    CHECK(all);
                }
            }
        }
    }
}

void testClothNormalsAndDeform() {
    DynamicMesh sheet = makeClothSheet(2.0f, 1.0f, 8, 4);
    CHECK(validateDynamicMesh(sheet, nullptr));
    CHECK(!sheet.skinned());
    std::vector<float> flat = sheet.rest.vertices;
    for (size_t i = 0; i < sheet.vertexCount(); ++i) {
        flat[i * kVertexFloats + 3] = 0.3f;
        flat[i * kVertexFloats + 4] = 0.3f;
        flat[i * kVertexFloats + 5] = 0.3f;
    }
    recomputeNormals(flat, sheet.rest.indices);
    for (size_t i = 0; i < sheet.vertexCount(); ++i) {
        const glm::vec3 n = normal(flat, i);
        CHECK(nearly(n.x, 0.0f) && nearly(n.y, 0.0f) && nearly(std::fabs(n.z), 1.0f));
    }
    // The front layer faces +z, the back layer -z.
    CHECK(normal(flat, 0).z > 0.9f && normal(flat, sheet.vertexCount() - 1).z < -0.9f);

    DeformSettings cloth;
    cloth.mode = DeformMode::Cloth;
    cloth.amplitude = 0.2f;
    cloth.pinY = 0.0f;
    cloth.pinRange = 1.0f;
    cloth.time = 0.7f;
    const glm::vec3 top = deformPoint(cloth, glm::vec3(0.5f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(glm::length(top - glm::vec3(0.5f, 0.0f, 0.0f)) < 1.0e-6f);
    float maxDisplacement = 0.0f;
    for (int i = 0; i < 200; ++i) {
        const glm::vec3 p(0.01f * static_cast<float>(i), -0.9f, 0.0f);
        cloth.time = 0.05f * static_cast<float>(i);
        maxDisplacement = std::max(maxDisplacement, glm::length(deformPoint(cloth, p, glm::vec3(0.0f, 0.0f, 1.0f)) - p));
    }
    CHECK(maxDisplacement > 0.05f);
    CHECK(maxDisplacement <= deformReach(cloth) + 1.0e-5f);

    DeformSettings wave;
    wave.mode = DeformMode::Wave;
    wave.amplitude = 0.0f;
    CHECK(glm::length(deformPoint(wave, glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(0.0f, 1.0f, 0.0f)) - glm::vec3(1.0f, 2.0f, 3.0f)) < 1.0e-6f);

    // Deformed + recomputed normals stay unit length.
    std::vector<float> out;
    cloth.recomputeNormals = true;
    deformMeshCpu(sheet, {}, {}, cloth, out);
    for (size_t i = 0; i < sheet.vertexCount(); ++i) CHECK(nearly(glm::length(normal(out, i)), 1.0f, 1.0e-3f));
}

void testBlendshapeBridge() {
    const DynamicMesh mesh = makeProceduralCharacter();
    BlendshapeWeights weights = blendshapeZero();
    weights[static_cast<size_t>(blendshapeIndex("jawOpen"))] = 0.75f;
    weights[static_cast<size_t>(blendshapeIndex("tongueOut"))] = 1.0f;
    std::vector<float> out;
    morphWeightsFromBlendshapes(mesh, weights, out);
    CHECK(out.size() == mesh.morphs.size());
    CHECK(nearly(out[static_cast<size_t>(mesh.findMorph("jawOpen"))], 0.75f));
    CHECK(nearly(std::accumulate(out.begin(), out.end(), 0.0f), 0.75f));

    BlendshapeLibrary library;
    const uint32_t clip = library.add(makeTalkClip());
    BlendshapePlayer player;
    player.play(clip);
    BlendshapeWeights playing = blendshapeZero();
    float jawMax = 0.0f;
    for (int i = 0; i < 40; ++i) {
        player.update(0.05f, library, playing);
        jawMax = std::max(jawMax, playing[static_cast<size_t>(blendshapeIndex("jawOpen"))]);
    }
    CHECK(jawMax > 0.5f);
}

std::string base64(const std::vector<uint8_t>& bytes) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t a = bytes[i], b = i + 1 < bytes.size() ? bytes[i + 1] : 0, c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;
        out += alphabet[(triple >> 18) & 63];
        out += alphabet[(triple >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(triple >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[triple & 63] : '=';
    }
    return out;
}

template <typename T>
void append(std::vector<uint8_t>& bytes, const T& value) {
    const auto* raw = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(T));
}

// A quad strip skinned to two joints (glTF 2.0, embedded buffer), plus one animation channel.
void testImportSkinned() {
    std::vector<uint8_t> buffer;
    const float positions[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 2, 0}, {1, 2, 0}};
    for (const auto& p : positions) for (float c : p) append(buffer, c);                       // 0..47
    for (int i = 0; i < 4; ++i) { for (float c : {0.0f, 0.0f, 1.0f}) append(buffer, c); }      // normals 48..95
    for (uint16_t index : {0, 1, 2, 2, 1, 3}) append(buffer, index);                           // 96..107
    for (int i = 0; i < 4; ++i) { for (uint8_t j : {0, 1, 0, 0}) append(buffer, j); }          // joints 108..123
    const float w[4][4] = {{1, 0, 0, 0}, {1, 0, 0, 0}, {0.5f, 0.5f, 0, 0}, {0.2f, 0.6f, 0, 0}}; // weights (last row unnormalised)
    for (const auto& row : w) for (float c : row) append(buffer, c);                           // 124..187
    const glm::mat4 identity(1.0f);
    glm::mat4 inverseSecond = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) append(buffer, identity[c][r]);   // 188..251
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) append(buffer, inverseSecond[c][r]); // 252..315
    for (float t : {0.0f, 1.0f}) append(buffer, t);                                            // times 316..323
    for (float c : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.7071f, 0.0f, 0.7071f}) append(buffer, c);  // rotations 324..355
    const std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],
"nodes":[{"name":"mesh","mesh":0,"skin":0},{"name":"root","children":[2]},{"name":"tip","translation":[0,1,0]}],
"skins":[{"joints":[1,2],"inverseBindMatrices":6,"skeleton":1}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"JOINTS_0":3,"WEIGHTS_0":4},"indices":2}]}],
"animations":[{"name":"bend","samplers":[{"input":7,"output":8,"interpolation":"LINEAR"}],"channels":[{"sampler":0,"target":{"node":2,"path":"rotation"}}]}],
"accessors":[
{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,2,0]},
{"bufferView":1,"componentType":5126,"count":4,"type":"VEC3"},
{"bufferView":2,"componentType":5123,"count":6,"type":"SCALAR"},
{"bufferView":3,"componentType":5121,"count":4,"type":"VEC4"},
{"bufferView":4,"componentType":5126,"count":4,"type":"VEC4"},
{"bufferView":5,"componentType":5126,"count":2,"type":"MAT4"},
{"bufferView":5,"componentType":5126,"count":2,"type":"MAT4"},
{"bufferView":6,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},
{"bufferView":7,"componentType":5126,"count":2,"type":"VEC4"}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":48},{"buffer":0,"byteOffset":96,"byteLength":12},
{"buffer":0,"byteOffset":108,"byteLength":16},{"buffer":0,"byteOffset":124,"byteLength":64},{"buffer":0,"byteOffset":188,"byteLength":128},
{"buffer":0,"byteOffset":316,"byteLength":8},{"buffer":0,"byteOffset":324,"byteLength":32}],
"buffers":[{"byteLength":356,"uri":"data:application/octet-stream;base64,)" + base64(buffer) + R"("}]})";
    const std::string path = testsupport::tempFilePath("lit_skin_", "quad.gltf");
    {
        std::ofstream file(path, std::ios::binary);
        file << json;
    }
    const auto imported = AssetManager::importSkinned(path);
    std::filesystem::remove(path);
    CHECK(imported.has_value());
    if (!imported) return;
    CHECK(imported->vertexCount() == 4);
    CHECK(imported->rest.indices.size() == 6);
    CHECK(imported->skinned());
    CHECK(imported->joints.size() >= 2);
    CHECK(imported->findJoint("root") >= 0 && imported->findJoint("tip") >= 0);
    const int tip = imported->findJoint("tip");
    CHECK(tip >= 0 && imported->joints[static_cast<size_t>(tip)].parent == imported->findJoint("root"));
    if (tip >= 0) CHECK(nearly(imported->joints[static_cast<size_t>(tip)].inverseBind[3][1], -1.0f));
    for (const SkinVertex& vertex : imported->skin) {
        float sum = 0.0f;
        for (float weight : vertex.weights) sum += weight;
        CHECK(nearly(sum, 1.0f));
    }
    // Vertex 3 had weights 0.2 / 0.6: normalised to 0.25 / 0.75 on the two joints.
    const SkinVertex& last = imported->skin[3];
    float onTip = 0.0f;
    for (size_t k = 0; k < 4; ++k) {
        if (tip >= 0 && last.joints[k] == static_cast<uint32_t>(tip)) onTip += last.weights[k];
    }
    CHECK(nearly(onTip, 0.75f, 1.0e-3f));
    CHECK(imported->clips.size() == 1);
    if (!imported->clips.empty()) {
        CHECK(nearly(imported->clips[0].duration, 1.0f, 1.0e-3f));
        CHECK(!imported->clips[0].channels.empty() && imported->clips[0].channels[0].rotation.size() == 2);
        const SkeletalClip clip = clipFromSkin(imported->clips[0]);
        CHECK(clip.tracks.size() == imported->clips[0].channels.size());
    }
}

}

int main() {
    testWeights();
    testValidate();
    testCharacter();
    testWinding();
    testPaletteAndBindPose();
    testRigidTranslation();
    testElbowBend();
    testMorphAndBounds();
    testClothNormalsAndDeform();
    testBlendshapeBridge();
    testImportSkinned();
    return testsupport::finish();
}
