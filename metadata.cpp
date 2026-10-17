#include "../include/audio_engine.h"
#include <iostream>
#include <fstream>
#include <cstdint>

AudioMetadata MetadataExtractor::extractFromFile(const std::string& filePath) {
    AudioMetadata meta;

    meta.title = "Unknown Title";
    meta.artist = "Unknown Artist";
    meta.album = "Unknown Album";
    meta.genre = "Unknown Genre";
    meta.durationSeconds = 0;
    meta.sampleRate = 44100;
    meta.channels = 2;
    meta.bitRate = 320;

    std::ifstream file(filePath, std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "Error opening file: " << filePath << std::endl;
        return meta;
    }

    char chunkID[4];
    file.read(chunkID, 4);

    if (file && std::string(chunkID, 4) == "RIFF") {
        file.seekg(22, std::ios::beg);

        uint16_t numChannels = 0;
        file.read(reinterpret_cast<char*>(&numChannels), sizeof(numChannels));

        uint32_t sampleRate = 0;
        file.read(reinterpret_cast<char*>(&sampleRate), sizeof(sampleRate));

        file.seekg(40, std::ios::beg);

        uint32_t dataSize = 0;
        file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));

        if (file && sampleRate > 0 && numChannels > 0) {
            meta.channels = numChannels;
            meta.sampleRate = sampleRate;
            meta.durationSeconds = static_cast<int>(
                dataSize / (sampleRate * numChannels * 2)
            );
        }

        size_t lastSlash = filePath.find_last_of("/\\");
        meta.title = (lastSlash == std::string::npos)
            ? filePath
            : filePath.substr(lastSlash + 1);
    } else {
        std::cout << "Non-WAV file detected. Using default metadata."
                  << std::endl;
    }

    file.close();
    return meta;
}
