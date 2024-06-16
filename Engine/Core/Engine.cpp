#include "../../include_all.h"
#include "Entity.cpp"

std::string getFileExtension(const std::string& filePath) {
    std::filesystem::path pathObj(filePath);

    if (pathObj.has_extension()) {
        return pathObj.extension().string();
    }
    
    return "no file extension";
}

const char* encryptFileString(const std::string& inputFile, const std::string& key) {
    std::ifstream inFile(inputFile, std::ios::binary);

    if (!inFile) {
        std::cerr << "Failed to open encrypted file." << std::endl;
        return nullptr;
    }

    size_t keyLength = key.size();
    size_t bufferSize = 4096;
    char buffer[4096];

    size_t bytesRead = 0;
    size_t keyIndex = 0;
    std::string encryptedData;

    while (inFile.good()) {
        inFile.read(buffer, bufferSize);
        bytesRead = inFile.gcount();

        for (size_t i = 0; i < bytesRead; ++i) {
            buffer[i] ^= key[keyIndex++];
            keyIndex %= keyLength;
        }

        encryptedData.append(buffer, bytesRead);
    }

    inFile.close();
    
    char* encryptedCString = new char[encryptedData.size() + 1];
    std::strcpy(encryptedCString, encryptedData.c_str());

    return encryptedCString;
}

const char* decryptFileString(const std::string& inputFile, const std::string& key) {
    return encryptFileString(inputFile, key); 
}

std::string readFileToString(const std::string& filename) {
    std::ifstream file(filename);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

HitInfo raycast(LitVector3 origin, LitVector3 direction, bool debug, std::vector<Entity> ignore) {
    HitInfo hitInfo;
    hitInfo.hit = false;

    Ray ray{origin, direction};

    if (debug)
        DrawRay(ray, RED);

    if (entitiesList.empty())
        return hitInfo;

    float minDistance = FLT_MAX;

    for (const Entity& entity : entitiesList)
    {
        if (std::find(ignore.begin(), ignore.end(), entity) != ignore.end())
            continue;
        
        if (!entity.collider)
            continue;

        RayCollision entityBounds = GetRayCollisionBox(ray, entity.bounds);

        for (int mesh_i = 0; mesh_i < entity.model.meshCount && entityBounds.hit; mesh_i++)
        {
            RayCollision meshHitInfo = GetRayCollisionMesh(ray, entity.model.meshes[mesh_i], entity.model.transform);

            if (meshHitInfo.hit && meshHitInfo.distance < minDistance)
            {
                minDistance = meshHitInfo.distance;
                
                hitInfo.hit = true;
                hitInfo.distance = minDistance;
                hitInfo.entity = const_cast<Entity*>(&entity);
                hitInfo.worldPoint = meshHitInfo.point;
                hitInfo.worldNormal = meshHitInfo.normal;
                hitInfo.hitColor = {
                    static_cast<unsigned char>(entity.surfaceMaterial.color.x * 255),
                    static_cast<unsigned char>(entity.surfaceMaterial.color.w * 255),
                    static_cast<unsigned char>(entity.surfaceMaterial.color.y * 255),
                    static_cast<unsigned char>(entity.surfaceMaterial.color.z * 255)
                };
            }
        }
    }

    return hitInfo;
}
