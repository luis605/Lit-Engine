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

bool bloomEnabled = false;
float bloomBrightness = 0.0f;
float bloomSamples = 3.0f;

Shader shader;
Shader instancingShader;
Shader downsamplerShader;
Shader upsamplerShader;
GLuint lightsBuffer;
GLuint renderPrevierLightsBuffer;
GLuint surfaceMaterialUBO;

Vector4 ambientLight = {1.0f, 1.0f, 1.0f, 1.0f};

RenderTexture downsamplerTexture;
RenderTexture upsamplerTexture;

void UpdateLightsBuffer(bool force, std::vector<LightStruct>& lightsVector, GLuint& buffer = lightsBuffer);
LightStruct& NewLight(const Vector3 position, const Color color, int type = LIGHT_POINT);

struct Light {
    int type = LIGHT_POINT;
    bool enabled = true;
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 relativePosition;
    alignas(16) glm::vec3 target;
    alignas(16) glm::vec3 direction = {0.4, 0.4, -0.4};
    alignas(16) glm::vec4 color;
    float attenuation = 0.001;
    float intensity = 3;
    float specularStrength = 0.5;
};

struct LightInfo {
    std::string name;
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
        cleanup();
    }

    SurfaceMaterialTexture& operator=(const SurfaceMaterialTexture& other) {
        if (this != &other) {
            cleanup();

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
                Texture2D fileTexture = LoadTexture(filePath.c_str());

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
                std::cerr << "Error loading file: " << e.what() << std::endl;
            }
        } else {
            throw std::runtime_error("File does not exist");
        }
    }

    // Check if it has a texture
    bool hasTexture() const {
        return IsTextureReady(staticTexture);
    }

    // Check if it has a video
    bool hasVideo() const {
        return videoTexture.hasVideoStream();
    }

    // Check if it is empty
    bool isEmpty() const {
        return activatedMode == -1;
    }

    // Get texture
    Texture2D& getTexture2D() {
        return staticTexture;
    }

    // Get video
    Video& getVideo() {
        return videoTexture;
    }

    void updateVideo() {
        videoTexture.PlayCurrentFrame();
    }

    Texture& getVideoTexture() {
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
    float SpecularIntensity = 0.5f;
    float DiffuseIntensity = 0.5f;
    float Roughness = 0.5f;
    float Metalness = 0.5f;
    alignas(16) glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

    fs::path diffuseTexturePath;
    fs::path normalTexturePath;
    fs::path roughnessTexturePath;
    fs::path aoTexturePath;

    SurfaceMaterialTexture diffuseTexture;
    SurfaceMaterialTexture normalTexture;
    SurfaceMaterialTexture roughnessTexture;
    SurfaceMaterialTexture aoTexture;
};

LightStruct& NewLight(const Vector3 position, const Color color, int type) {
    glm::vec3 lightsPosition = glm::vec3(position.x, position.y, position.z);
    glm::vec4 lightsColor = glm::vec4(color.r/255, color.g/255, color.b/255, color.a/255);

    LightStruct lightStruct;
    lightStruct.lightInfo.name = "New Light";
    lightStruct.id = lights.size() + entitiesListPregame.size();
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
        lightsData.push_back(lightStruct.light);
    }

    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, lightsData.data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);

    int lightsCount = static_cast<int>(lightsVector.size());
    SetShaderValue(shader, GetShaderLocation(shader, "lightsCount"), &lightsCount, SHADER_UNIFORM_INT);
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