/**
 * @file MetadataReader.cpp
 * @brief Binary metadata parser and cache implementation.
 *
 * DSA Concepts:
 *  - Binary File Parsing: Reading raw byte streams, decoding Big-Endian integers,
 *    navigating nested hierarchical atoms and tag frame structures.
 *  - Hash Map (std::unordered_map): Fast O(1) cache so subsequent queries for the
 *    same media file bypass expensive disk I/O.
 *
 * Author: Adhiraj Singh
 */

#include "MetadataReader.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace mediax {

MetadataReader& MetadataReader::instance() {
    static MetadataReader s_instance;
    return s_instance;
}

void MetadataReader::clearCache() {
    cache_.clear();
}

bool MetadataReader::isSupported(const std::string& filePath) {
    std::string ext = getExtension(filePath);
    return (ext == "mp3" || ext == "mp4" || ext == "m4a" || ext == "m4v" ||
            ext == "wav" || ext == "ogg" || ext == "flac" || ext == "webm" ||
            ext == "mkv" || ext == "avi");
}

MediaKind MetadataReader::detectKind(const std::string& filePath) {
    std::string ext = getExtension(filePath);
    if (ext == "mp4" || ext == "m4v" || ext == "mkv" || ext == "avi" || ext == "webm") {
        return MediaKind::VIDEO;
    }
    return MediaKind::AUDIO;
}

MediaItem MetadataReader::extract(const std::string& filePath) {
    // 1. Check in-memory hash map cache (O(1) average lookup)
    auto it = cache_.find(filePath);
    if (it != cache_.end()) {
        return it->second;
    }

    std::string ext = getExtension(filePath);
    MediaItem item;

    if (ext == "mp3") {
        item = parseMP3(filePath);
    } else if (ext == "mp4" || ext == "m4a" || ext == "m4v") {
        item = parseMP4(filePath);
    } else {
        item = parseFallback(filePath);
    }

    // Save in hash map cache
    cache_[filePath] = item;
    return item;
}

// ─────────────────────────────────────────────────────────────────────────────
// §1  MP3 / ID3v2 Parsing
// ─────────────────────────────────────────────────────────────────────────────

bool MetadataReader::readID3v2Header(std::ifstream& f, uint32_t& tagSize) {
    char header[10];
    f.seekg(0, std::ios::beg);
    if (!f.read(header, 10)) return false;

    // First 3 bytes must be "ID3"
    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') {
        return false;
    }

    // Bytes 6-9 are the 28-bit syncsafe integer size (MSB of each byte is 0)
    // Formula: (b0 & 0x7F) << 21 | (b1 & 0x7F) << 14 | (b2 & 0x7F) << 7 | (b3 & 0x7F)
    uint8_t* b = reinterpret_cast<uint8_t*>(header + 6);
    tagSize = ((b[0] & 0x7F) << 21) |
              ((b[1] & 0x7F) << 14) |
              ((b[2] & 0x7F) << 7)  |
              (b[3] & 0x7F);

    return true;
}

void MetadataReader::parseID3v2Frames(std::ifstream& f, uint32_t tagSize, MediaItem& out) {
    // Read up to tagSize bytes of frames
    uint32_t bytesRead = 0;
    while (bytesRead + 10 < tagSize && f.good()) {
        char frameHeader[10];
        if (!f.read(frameHeader, 10)) break;
        bytesRead += 10;

        // Frame ID (4 chars)
        std::string frameId(frameHeader, 4);
        if (frameId[0] < 'A' || frameId[0] > 'Z') break; // padding reached

        // Frame size (Big Endian uint32)
        uint8_t* sb = reinterpret_cast<uint8_t*>(frameHeader + 4);
        uint32_t fsize = (static_cast<uint32_t>(sb[0]) << 24) |
                         (static_cast<uint32_t>(sb[1]) << 16) |
                         (static_cast<uint32_t>(sb[2]) << 8)  |
                         static_cast<uint32_t>(sb[3]);

        if (fsize == 0 || bytesRead + fsize > tagSize + 10) break;

        std::vector<uint8_t> data(fsize);
        if (!f.read(reinterpret_cast<char*>(data.data()), fsize)) break;
        bytesRead += fsize;

        // Frame mappings
        if (frameId == "TIT2") {
            out.title = bytesToString(data, 1); // skip encoding byte
        } else if (frameId == "TPE1") {
            out.artist = bytesToString(data, 1);
        } else if (frameId == "TALB") {
            out.album = bytesToString(data, 1);
        } else if (frameId == "APIC") {
            // Attached picture: [encoding][mime][pic_type][description][image_data]
            size_t pos = 1;
            std::string mime = "";
            while (pos < data.size() && data[pos] != 0) {
                mime += static_cast<char>(data[pos++]);
            }
            if (pos < data.size()) pos++; // skip null
            if (pos < data.size()) pos++; // skip picture type
            while (pos < data.size() && data[pos] != 0) {
                pos++; // skip description
            }
            if (pos < data.size()) pos++; // skip null

            if (pos < data.size()) {
                std::vector<uint8_t> imgBytes(data.begin() + pos, data.end());
                if (mime.empty()) mime = "image/jpeg";
                out.artDataUri = "data:" + mime + ";base64," + base64Encode(imgBytes);
            }
        }
    }
}

MediaItem MetadataReader::parseMP3(const std::string& path) {
    MediaItem item;
    item.filePath = path;
    item.format = "MP3";
    item.kind = MediaKind::AUDIO;
    item.fileSizeBytes = getFileSize(path);
    item.title = filenameWithoutExt(path);
    item.artist = "Unknown Artist";
    item.resolutionW = 0;
    item.resolutionH = 0;

    std::ifstream f(path, std::ios::binary);
    if (f.is_open()) {
        uint32_t tagSize = 0;
        if (readID3v2Header(f, tagSize)) {
            parseID3v2Frames(f, tagSize, item);
        }
    }

    // Default duration estimation: approx CBR 320 kbps (duration = size * 8 / (320 * 1000))
    if (item.durationSecs <= 0.0 && item.fileSizeBytes > 0) {
        item.durationSecs = static_cast<double>(item.fileSizeBytes * 8) / (320.0 * 1000.0);
    }
    item.bitrate = "320 kbps";
    return item;
}

// ─────────────────────────────────────────────────────────────────────────────
// §2  MP4 Atom Parsing
// ─────────────────────────────────────────────────────────────────────────────

void MetadataReader::walkAtoms(std::ifstream& f, long long limit, MediaItem& out) {
    while (f.tellg() < limit && f.good()) {
        long long atomStart = f.tellg();
        uint32_t size32 = readBE32(f);
        char typeBuf[5] = {0};
        if (!f.read(typeBuf, 4)) break;
        std::string atomType(typeBuf, 4);

        long long atomSize = size32;
        if (atomSize == 1) { // Extended 64-bit size
            atomSize = readBE64(f);
        } else if (atomSize == 0) {
            atomSize = limit - atomStart;
        }

        if (atomSize <= 8) break;

        long long atomEnd = atomStart + atomSize;

        // Container atoms: traverse into them
        if (atomType == "moov" || atomType == "udta" || atomType == "meta" ||
            atomType == "ilst" || atomType == "trak" || atomType == "mdia") {
            if (atomType == "meta") {
                // meta atom has 4 bytes flags/version before inner atoms
                f.seekg(4, std::ios::cur);
            }
            walkAtoms(f, atomEnd, out);
        } else if (atomType == "mvhd") {
            // Read duration & timescale: [version:1][flags:3][creation:4][mod:4][timescale:4][duration:4]
            char v[4];
            f.read(v, 4);
            int version = static_cast<uint8_t>(v[0]);
            if (version == 0) {
                f.seekg(8, std::ios::cur);
                uint32_t timescale = readBE32(f);
                uint32_t duration = readBE32(f);
                if (timescale > 0) {
                    out.durationSecs = static_cast<double>(duration) / static_cast<double>(timescale);
                }
            }
        } else if (atomType == "\xA9nam" || atomType == "©nam") { // Title
            f.seekg(16, std::ios::cur); // skip data atom wrapper
            long long remaining = atomEnd - f.tellg();
            if (remaining > 0 && remaining < 1024) {
                std::vector<uint8_t> d(remaining);
                f.read(reinterpret_cast<char*>(d.data()), remaining);
                out.title = bytesToString(d);
            }
        } else if (atomType == "\xA9" "ART" || atomType == "©ART") { // Artist
            f.seekg(16, std::ios::cur);
            long long remaining = atomEnd - f.tellg();
            if (remaining > 0 && remaining < 1024) {
                std::vector<uint8_t> d(remaining);
                f.read(reinterpret_cast<char*>(d.data()), remaining);
                out.artist = bytesToString(d);
            }
        } else if (atomType == "\xA9" "alb" || atomType == "©alb") { // Album
            f.seekg(16, std::ios::cur);
            long long remaining = atomEnd - f.tellg();
            if (remaining > 0 && remaining < 1024) {
                std::vector<uint8_t> d(remaining);
                f.read(reinterpret_cast<char*>(d.data()), remaining);
                out.album = bytesToString(d);
            }
        } else if (atomType == "covr") { // Cover Art
            f.seekg(16, std::ios::cur);
            long long remaining = atomEnd - f.tellg();
            if (remaining > 0 && remaining < 10 * 1024 * 1024) { // max 10MB
                std::vector<uint8_t> d(remaining);
                f.read(reinterpret_cast<char*>(d.data()), remaining);
                out.artDataUri = "data:image/jpeg;base64," + base64Encode(d);
            }
        }

        f.seekg(atomEnd, std::ios::beg);
    }
}

MediaItem MetadataReader::parseMP4(const std::string& path) {
    MediaItem item;
    item.filePath = path;
    item.format = "MP4";
    item.kind = detectKind(path);
    item.fileSizeBytes = getFileSize(path);
    item.title = filenameWithoutExt(path);
    item.artist = "Unknown Artist";
    if (item.kind == MediaKind::VIDEO) {
        item.resolutionW = 1920;
        item.resolutionH = 1080;
    }

    std::ifstream f(path, std::ios::binary);
    if (f.is_open()) {
        walkAtoms(f, item.fileSizeBytes, item);
    }

    if (item.durationSecs <= 0.0 && item.fileSizeBytes > 0) {
        item.durationSecs = 180.0; // default standard 3 min
    }
    item.bitrate = (item.kind == MediaKind::VIDEO) ? "4500 kbps" : "256 kbps";
    return item;
}

MediaItem MetadataReader::parseFallback(const std::string& path) {
    MediaItem item;
    item.filePath = path;
    item.format = getExtension(path);
    std::transform(item.format.begin(), item.format.end(), item.format.begin(), ::toupper);
    item.kind = detectKind(path);
    item.fileSizeBytes = getFileSize(path);
    item.title = filenameWithoutExt(path);
    item.artist = "Unknown Artist";
    item.durationSecs = 180.0;
    item.bitrate = "320 kbps";
    return item;
}

// ─────────────────────────────────────────────────────────────────────────────
// §3  Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

uint32_t MetadataReader::readBE32(std::ifstream& f) {
    uint8_t b[4];
    if (!f.read(reinterpret_cast<char*>(b), 4)) return 0;
    return (static_cast<uint32_t>(b[0]) << 24) |
           (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8)  |
           static_cast<uint32_t>(b[3]);
}

uint64_t MetadataReader::readBE64(std::ifstream& f) {
    uint8_t b[8];
    if (!f.read(reinterpret_cast<char*>(b), 8)) return 0;
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val = (val << 8) | static_cast<uint64_t>(b[i]);
    }
    return val;
}

std::string MetadataReader::bytesToString(const std::vector<uint8_t>& data, size_t offset) {
    if (offset >= data.size()) return "";
    std::string s;
    for (size_t i = offset; i < data.size(); ++i) {
        if (data[i] == 0) break; // null terminator
        s += static_cast<char>(data[i]);
    }
    return s;
}

std::string MetadataReader::base64Encode(const std::vector<uint8_t>& data) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i < data.size()) {
        uint32_t octet_a = i < data.size() ? data[i++] : 0;
        uint32_t octet_b = i < data.size() ? data[i++] : 0;
        uint32_t octet_c = i < data.size() ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out.push_back(table[(triple >> 18) & 0x3F]);
        out.push_back(table[(triple >> 12) & 0x3F]);
        out.push_back(i > data.size() + 1 ? '=' : table[(triple >> 6) & 0x3F]);
        out.push_back(i > data.size() ? '=' : table[triple & 0x3F]);
    }
    return out;
}

long long MetadataReader::getFileSize(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return 0;
    return f.tellg();
}

std::string MetadataReader::getExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot == path.length() - 1) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string MetadataReader::filenameWithoutExt(const std::string& path) {
    size_t sep = path.find_last_of("/\\");
    std::string filename = (sep == std::string::npos) ? path : path.substr(sep + 1);
    size_t dot = filename.find_last_of('.');
    return (dot == std::string::npos) ? filename : filename.substr(0, dot);
}

std::string MetadataReader::estimateBitrate(long long fileBytes, double durationSecs) {
    if (durationSecs <= 0.0) return "320 kbps";
    long long kbps = static_cast<long long>((fileBytes * 8.0) / (durationSecs * 1000.0));
    return std::to_string(kbps) + " kbps";
}

} // namespace mediax

