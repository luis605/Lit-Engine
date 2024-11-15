#ifndef LIGHTS_H
#define LIGHTS_H

enum {
    LIGHT_DIRECTIONAL = 0,
    LIGHT_POINT = 1,
    LIGHT_SPOT = 2
} LightType;


struct SurfaceMaterial;
struct AdditionalLightInfo;
struct LightStruct;

std::vector<LightStruct> lights;
std::vector<LightStruct> renderModelPreviewerLights;

Texture2D lightTexture;

int shadowMapWidth = 1024;
int shadowMapHeight = 1024;

unsigned int depthMapFBO;
GLuint downsamplerPixelCountBuffer;
GLuint downsamplerLuminanceBuffer;

bool bloomEnabled = false;
float bloomThreshold = 0.2f;
float bloomIntensity = 0.5f;
int kernelSize = 1;
float prevExposure = 0.0f;
int downsamplerFactor = 4;

Shader shader;
Shader instancingShader;
Shader horizontalBlurShader;
Shader verticalBlurShader;
Shader downsamplerShader;
Shader upsamplerShader;
GLuint lightsBuffer;
GLuint renderPrevierLightsBuffer;
GLuint surfaceMaterialUBO;

Vector4 ambientLight = {1.0f, 1.0f, 1.0f, 1.0f};

RenderTexture verticalBlurTexture;
RenderTexture horizontalBlurTexture;
RenderTexture upsamplerTexture;
RenderTexture downsamplerTexture;

void UpdateLightsBuffer(bool force, std::vector<LightStruct>& lightsVector, GLuint& buffer = lightsBuffer);
LightStruct& NewLight(const Vector3 position, const Color color, int type = LIGHT_POINT, int id = -1);

struct Light {
    int type = LIGHT_POINT;
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 relativePosition;
    alignas(16) glm::vec3 target;
    alignas(16) glm::vec3 direction = {0.4, 0.4, -0.4};
    alignas(16) glm::vec4 color;
    alignas(4) float attenuation = 0.001f;
    alignas(4) float intensity = 3.0f;
    alignas(4) float specularStrength = 0.5f;
    alignas(4) float radius = 1.5f;
    alignas(4) float innerCutoff = 1.0f;
    alignas(4) float outerCutoff = 3.0f;
};

struct LightInfo {
    std::string name;
    bool enabled = true;
};

struct LightStruct {
    Light light;
    LightInfo lightInfo;
    Entity* parent = nullptr;
    int id;
    bool isChild = false;

    LightStruct() {
    }

    bool operator==(const LightStruct& other) const {
        return (int)this->id == (int)other.id;
    }
};

struct SurfaceMaterialTexture {
    Texture2D staticTexture = { 0 };
    Video videoTexture;
    int activatedMode = -1;

    SurfaceMaterialTexture() = default;

    SurfaceMaterialTexture(fs::path& filePath) {
        loadFromFile(filePath);
    }

    // Destructor
    ~SurfaceMaterialTexture() {
        // cleanup();  // Commented: Would be called when not needed. Happened when adding a new entity
    }

    SurfaceMaterialTexture& operator=(const SurfaceMaterialTexture& other) {
        if (this != &other) {
            // cleanup(); // Commented: Would be called when not needed. Requires review. Happened when running game (assign operator) and when adding a new entity

            staticTexture = other.staticTexture;
            videoTexture = other.videoTexture;
            activatedMode = other.activatedMode;
        }
        return *this;
    }

    SurfaceMaterialTexture& operator=(const fs::path& filePath) {
        loadFromFile(filePath);
        return *this;
    }

    // Load from file
    void loadFromFile(const fs::path& filePath) {
        if (fs::exists(filePath)) {
            try {
                Texture2D fileTexture = LoadTexture(filePath.string().c_str());

                if (IsTextureReady(fileTexture)) {
                    staticTexture = fileTexture;
                    activatedMode = 0; // Texture mode
                } else {
                    UnloadTexture(fileTexture);
                    videoTexture = Video();
                    videoTexture.openVideo(filePath.string().c_str());
                    activatedMode = 1; // Video mode

                    if (!videoTexture.hasVideoStream()) {
                        std::cerr << "Failure on loading video stream" << std::endl;
                    }
                }
            } catch (const std::exception &e) {
                TraceLog(LOG_WARNING, "Error loading texture or video file.");
            }
        } else {
            TraceLog(LOG_WARNING, "Texture or video does not exist.");
        }
    }

    // Check if it has a texture
    bool hasTexture() const {
        return IsTextureReady(staticTexture);
    }

    // Check if it has a video
    const bool hasVideo() const {
        return videoTexture.hasVideoStream();
    }

    // Check if it is empty
    const bool isEmpty() const {
        return activatedMode == -1;
    }

    // Get texture
    const Texture2D& getTexture2D() const {
        return staticTexture;
    }

    // Get video
    Video& getVideo() {
        return videoTexture;
    }

    void updateVideo() {
        videoTexture.PlayCurrentFrame();
    }

    Texture2D& getVideoTexture() {
        return videoTexture.getTexture();
    }

    // Cleanup resources
    void cleanup() {
        if (hasTexture()) {
            UnloadTexture(staticTexture);
            staticTexture = { 0 };
        } else if (hasVideo()) {
            videoTexture.cleanup();
        }
        activatedMode = -1;
    }
};


struct SurfaceMaterial {
    alignas(4) float specularIntensity = 0.5f;
    alignas(4) float albedoIntensity = 0.5f;
    alignas(4) float roughness = 0.5f;
    alignas(4) float metalness = 0.5f;
    alignas(4) float clearCoat = 0.0f;
    alignas(4) float clearCoatRoughness = 0.0f;
    alignas(4) float subsurface = 0.0f;
    alignas(4) float anisotropy = 0.0f;
    alignas(4) float transmission = 0.0f;
    alignas(4) float ior = 0.0f;
    alignas(4) float thickness = 0.0f;
    alignas(4) float heightScale = 0.0f;
    alignas(4) float emissiveIntensity = 0.0f;
    alignas(4) float aoStrength = 0.5f;

    alignas(16) glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

    fs::path albedoTexturePath;
    fs::path normalTexturePath;
    fs::path roughnessTexturePath;
    fs::path aoTexturePath;
    fs::path heightTexturePath;
    fs::path metallicTexturePath;
    fs::path emissiveTexturePath;

    SurfaceMaterialTexture albedoTexture;
    SurfaceMaterialTexture normalTexture;
    SurfaceMaterialTexture roughnessTexture;
    SurfaceMaterialTexture aoTexture;
    SurfaceMaterialTexture heightTexture;
    SurfaceMaterialTexture metallicTexture;
    SurfaceMaterialTexture emissiveTexture;
};

LightStruct& NewLight(const Vector3 position, const Color color, int type, int id) {
    glm::vec3 lightsPosition = glm::vec3(position.x, position.y, position.z);
    glm::vec4 lightsColor = glm::vec4(color.r/255, color.g/255, color.b/255, color.a/255);

    LightStruct lightStruct;
    lightStruct.lightInfo.name = "New Light";

    if (id == -1) lightStruct.id = lights.size() + entitiesListPregame.size();
    else          lightStruct.id = id;

    lightStruct.light.type = type;
    lightStruct.light.position = lightsPosition;
    lightStruct.light.color = lightsColor;

    lightIdToIndexMap[lightStruct.id] = lights.size();

    lights.emplace_back(std::move(lightStruct));

    return lights.back();
}

void UpdateLightsBuffer(bool force, std::vector<LightStruct>& lightsVector, GLuint& buffer) {
    if (lightsVector.empty() && !force)
        return;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);

    size_t bufferSize = sizeof(Light) * lightsVector.size();
    GLsizeiptr currentBufferSize;
    glGetBufferParameteri64v(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &currentBufferSize);

    if (bufferSize != static_cast<size_t>(currentBufferSize)) {
        glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);
    }

    std::vector<Light> lightsData;
    lightsData.reserve(lightsVector.size());

    for (const auto& lightStruct : lightsVector) {
        if (lightStruct.lightInfo.enabled)
            lightsData.push_back(lightStruct.light);
    }

    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, lightsData.data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);

    int lightsCount = static_cast<int>(lightsVector.size());
    SetShaderValue(shader, GetUniformLocation(shader, "lightsCount"), &lightsCount, SHADER_UNIFORM_INT);
}

void removeLight(int id) {
    auto it = lightIdToIndexMap.find(id);
    if (it != lightIdToIndexMap.end()) {
        size_t index = it->second;
        lightIdToIndexMap.erase(it);

        if (index != lights.size() - 1) {
            std::swap(lights[index], lights.back());
            lightIdToIndexMap[lights[index].id] = index;
        }
        lights.pop_back();
    }
}

int getIdFromLight(const LightStruct& lightStruct) {
    auto it = std::find(lights.begin(), lights.end(), lightStruct);
    if (it != lights.end()) {
        return it->id;
    }
    return -1;
}

LightStruct* getLightById(int id) {
    auto it = lightIdToIndexMap.find(id);
    if (it != lightIdToIndexMap.end()) {
        return &lights[it->second];
    }
    return nullptr;
}

#endif