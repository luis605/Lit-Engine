#pragma once
#include "../include_all.h"

void encryptFile(const std::string& inputFile, const std::string& outputFile, const std::string& key) {
    std::ifstream inFile(inputFile, std::ios::binary);
    std::ofstream outFile(outputFile, std::ios::binary);

    if (!inFile || !outFile) {
        std::cerr << "Failed to open shader files while exporting." << std::endl;
        return;
    }

    size_t keyLength = key.size();
    size_t bufferSize = 4096;
    char buffer[4096];

    size_t bytesRead = 0;
    size_t keyIndex = 0;

    while (inFile.good()) {
        inFile.read(buffer, bufferSize);
        bytesRead = inFile.gcount();

        for (size_t i = 0; i < bytesRead; ++i) {
            buffer[i] ^= key[keyIndex++];
            keyIndex %= keyLength;
        }

        outFile.write(buffer, bytesRead);
    }

    std::cout << "File encrypted successfully." << std::endl;
}

void decryptFile(const std::string& inputFile, const std::string& outputFile, const std::string& key) {
    encryptFile(inputFile, outputFile, key); // XOR decryption is the same as encryption
}

void createFolder(string directoryName)
{
    if (!std::filesystem::exists(directoryName)) {
        if (std::filesystem::create_directory(directoryName)) {
            std::cout << "Directory created successfully." << std::endl;
        } else {
            std::cerr << "Failed to create directory." << std::endl;
            return;
        }
    } else {
        std::cout << "Directory already exists." << std::endl;
    }
}

int BuildProject() {
    createFolder("exported_game");
    createFolder("exported_game/shaders");
    createFolder("exported_game/assets");




    std::filesystem::copy("project", "exported_game/project", std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy("project.json", "exported_game/project.json", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy("assets/images/skybox/default skybox.hdr", "exported_game/assets/default skybox.hdr", std::filesystem::copy_options::overwrite_existing);

    std::filesystem::copy("Engine/Lighting/shaders/skybox.vs", "exported_game/shaders/skybox.vs", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy("Engine/Lighting/shaders/skybox.fs", "exported_game/shaders/skybox.fs", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy("Engine/Lighting/shaders/cubemap.fs", "exported_game/shaders/cubemap.fs", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy("Engine/Lighting/shaders/cubemap.vs", "exported_game/shaders/cubemap.vs", std::filesystem::copy_options::overwrite_existing);
    
    string key_a = "141b5aceaaa5582ec3efb9a17cac2da5e52bbc1057f776e99a56a064f5ea40d5f8689b7542c4d0e9d6d7163b9dee7725369742a54905ac95c74be5cb1435fdb726fead2437675eaa13bc77ced8fb9cc6108d4a247a2b37b76a6e0bf41916fcc98ee5f85db11ecb52b0d94b5fbab58b1f4814ed49e761a7fb9dfb0960f00ecf8c87989b8e92a630680128688fa7606994e3be12734868716f9df27674700a2cb37440afe131e570a4ee9e7e867aab18a44ee972956b7bd728f9b937c973b9726f6bdd56090d720e6fa31c70b31e0216739cde4210bcd93671c1e8edb752b32f782b62eab4d77a51e228a6b6ac185d7639bd037f9195c3f05c5d2198947621814827f2d99dd7c2821e76635a845203f42060e5a9a494482afab1c42c23ba5f317f250321c7713c2ce19fe7a3957ce439f4782dbee3d418aebe08314a4d6ac7b3d987696d39600c5777f555a8dc99f2953ab45b0687efa1a77d8e5b448b37a137f2849c9b76fec98765523869c22a3453c214ec8e8827acdded27c37d96017fbf862a405b4b06fe0e815e09ed5288ccd9139e67c7feed3e7306f621976b9d3ba917d19ef4a13490f9e2af925996f59a87uihjoklas9emyuikw75igeturf7unftyngl635n4554hs23d2453pfds";
    std::string inputFile_a = "exported_game/scripts.json";
    std::string outputFile_a = "exported_game/encryptedScripts.json";

    encryptFile(inputFile_a, outputFile_a, key_a);

    // std::filesystem::remove("exported_game/scripts.json");


#ifdef _WIN32
    std::filesystem::copy("swscale-7.dll", "exported_game/swscale-7.dll", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy("avformat-60.dll", "exported_game/avformat-60.dll", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy("avutil-58.dll", "exported_game/avutil-58.dll", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy("swresample-4.dll", "exported_game/swresample-4.dll", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy("avcodec-60.dll", "exported_game/avcodec-60.dll", std::filesystem::copy_options::overwrite_existing);

#endif

    std::cout << "Game Sucessfully Exported" << std::endl;
    return 0;
}