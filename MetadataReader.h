#pragma once
/**
 * @file MetadataReader.h
 * @brief Media metadata extraction for audio and video files.
 *
 * DSA Concepts:
 *  - Binary File Parsing: Decoding ID3v2 tags (MP3) and Atom boxes (MP4/M4A).
 *  - Hash Map (std::unordered_map): In-memory caching for O(1) average lookup of file metadata.
 *
 * Author: Adhiraj Singh
 */

#include "MediaItem.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <fstream>

namespace mediax {

class MetadataReader {
public:
    /// Singleton access
    static MetadataReader& instance();

    /// Extract metadata from file path, using cache when available
    MediaItem extract(const std::string& filePath);

    /// Clear cache entries
    void clearCache();

    /// Check if format is supported by extension
    static bool isSupported(const std::string& filePath);

    /// Detect media kind (AUDIO / VIDEO) based on extension
    static MediaKind detectKind(const std::string& filePath);

private:
    MetadataReader() = default;

    // Parser routines
    MediaItem parseMP3(const std::string& path);
    bool readID3v2Header(std::ifstream& f, uint32_t& tagSize);
    void parseID3v2Frames(std::ifstream& f, uint32_t tagSize, MediaItem& out);

    MediaItem parseMP4(const std::string& path);
    void walkAtoms(std::ifstream& f, long long limit, MediaItem& out);

    MediaItem parseFallback(const std::string& path);

    // Helpers
    static uint32_t readBE32(std::ifstream& f);
    static uint64_t readBE64(std::ifstream& f);
    static std::string bytesToString(const std::vector<uint8_t>& data, size_t offset = 0);
    static std::string base64Encode(const std::vector<uint8_t>& data);
    static long long getFileSize(const std::string& path);
    static std::string getExtension(const std::string& path);
    static std::string filenameWithoutExt(const std::string& path);
    static std::string estimateBitrate(long long fileBytes, double durationSecs);

    // In-memory metadata cache (Hash Map: O(1) average lookup)
    std::unordered_map<std::string, MediaItem> cache_;
};

} // namespace mediax

