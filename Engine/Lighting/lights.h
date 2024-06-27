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

    LightStruct(std::string newName, int id) {
        lightInfo.name = newName;
        this->id = id;
    }

    bool operator==(const LightStruct& other) const {
        return (int)this->id == (int)other.id;
    }
};

struct SurfaceMaterialTexture {
    std::variant<std::monostate, Texture2D, VideoPlayer> texture;
    int activatedMode = -1;

    // Default constructor
    SurfaceMaterialTexture() = default;

    // Constructor from path (or const char*)
    SurfaceMaterialTexture(const fs::path& filePath) {
        if (fs::exists(filePath)) {
            Texture2D fileTexture = LoadTexture(filePath.c_str());

            if (IsTextureReady(fileTexture)) {
                texture = fileTexture;
                activatedMode = 0; // Texture mode
            } else {
                texture = filePath.c_str();
                activatedMode = 1; // Video mode
                UnloadTexture(fileTexture);
            }
        } else {
            throw std::runtime_error("File does not exist");
        }
    }

    // Copy assignment operator
    SurfaceMaterialTexture& operator=(const SurfaceMaterialTexture& other) {
        if (this != &other) {
            if (other.hasTexture()) {
                texture = std::get<Texture2D>(other.texture);
            } else if (other.hasVideoPlayer()) {
                texture = std::get<VideoPlayer>(other.texture);
            } else {
                texture = std::monostate{};
            }
            activatedMode = other.activatedMode;
        }
        return *this;
    }

    // Assignment from path (or const char*)
    SurfaceMaterialTexture& operator=(const fs::path& filePath) {
        if (fs::exists(filePath)) {
            Texture2D fileTexture = LoadTexture(filePath.c_str());

            if (IsTextureReady(fileTexture)) {
                texture = fileTexture;
                activatedMode = 0; // Texture mode
            } else {
                texture = filePath.c_str();
                activatedMode = 1; // Video mode
                UnloadTexture(fileTexture);
            }
        } else {
            throw std::runtime_error("File does not exist");
        }
        return *this;
    }

    bool hasTexture() const {
        return std::holds_alternative<Texture2D>(texture);
    }

    bool hasVideoPlayer() const {
        return std::holds_alternative<VideoPlayer>(texture);
    }

    bool isEmpty() const {
        return this->texture.valueless_by_exception() || std::holds_alternative<std::monostate>(this->texture);
    }

    // Getter functions
    Texture2D& getTexture2D() {
        return *std::get_if<Texture2D>(&texture);
    }

    VideoPlayer& getVideoPlayer() {
        return *std::get_if<VideoPlayer>(&texture);
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

    LightStruct lightStruct{"New Light", (int)(lights.size() + entitiesListPregame.size() + 1)};
    lightStruct.light.type = type;
    lightStruct.light.position = lightsPosition;
    lightStruct.light.color = lightsColor;

    lights.emplace_back(lightStruct);

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

#endif