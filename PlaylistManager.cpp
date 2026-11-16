/**
 * @file PlaylistManager.cpp
 * @brief Implementation of playlist manager operations and serialization.
 *
 * DSA Concepts:
 *  - Dynamic Array (std::vector): Playlist storage with O(1) amortized insertion,
 *    in-place reordering and deletion.
 *  - Linear Search: O(n) search by playlist ID and track ID.
 *  - Serialization & Parsing: File I/O format "id|name|id1,id2,id3" and JSON serialization
 *    for communicating with the web UI bridge.
 *
 * Author: Yatharth Pant
 */

#include "PlaylistManager.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace mediax {

PlaylistManager& PlaylistManager::instance() {
    static PlaylistManager s_instance;
    return s_instance;
}

PlaylistManager::PlaylistManager() : nextId_(1) {
    // Initialize default playlists matching web UI defaults
    playlists_.push_back({"pl-hyperloop", "HyperLoop", {}});
    playlists_.push_back({"pl-1", "Favourites", {}});
    playlists_.push_back({"pl-2", "Study Mix", {}});
    nextId_ = 3;
}

std::string PlaylistManager::generateId() {
    return "pl-" + std::to_string(nextId_++);
}

const std::vector<Playlist>& PlaylistManager::getAll() const {
    return playlists_;
}

Playlist* PlaylistManager::findById(const std::string& id) {
    // Linear scan: O(n)
    for (auto& pl : playlists_) {
        if (pl.id == id) return &pl;
    }
    return nullptr;
}

const Playlist* PlaylistManager::findById(const std::string& id) const {
    for (const auto& pl : playlists_) {
        if (pl.id == id) return &pl;
    }
    return nullptr;
}

std::vector<std::string> PlaylistManager::getTrackIds(const std::string& playlistId) const {
    const Playlist* pl = findById(playlistId);
    return pl ? pl->trackIds : std::vector<std::string>{};
}

Playlist& PlaylistManager::createPlaylist(const std::string& name) {
    Playlist pl;
    pl.id = generateId();
    pl.name = name.empty() ? "New Playlist" : name;
    playlists_.push_back(pl); // O(1) amortized
    return playlists_.back();
}

bool PlaylistManager::renamePlaylist(const std::string& id, const std::string& newName) {
    Playlist* pl = findById(id);
    if (!pl || newName.empty()) return false;
    pl->name = newName;
    return true;
}

bool PlaylistManager::deletePlaylist(const std::string& id) {
    // Erase-remove idiom on vector: O(n)
    auto it = std::remove_if(playlists_.begin(), playlists_.end(),
                             [&id](const Playlist& p) { return p.id == id; });
    if (it != playlists_.end()) {
        playlists_.erase(it, playlists_.end());
        return true;
    }
    return false;
}

Playlist PlaylistManager::duplicatePlaylist(const std::string& id) {
    Playlist* orig = findById(id);
    if (!orig) return Playlist{};

    Playlist copy;
    copy.id = generateId();
    copy.name = orig->name + " (Copy)";
    copy.trackIds = orig->trackIds;
    playlists_.push_back(copy);
    return copy;
}

bool PlaylistManager::addTrack(const std::string& playlistId, const std::string& mediaId) {
    Playlist* pl = findById(playlistId);
    if (!pl || mediaId.empty()) return false;

    // Prevent duplicate tracks in the same playlist
    if (std::find(pl->trackIds.begin(), pl->trackIds.end(), mediaId) != pl->trackIds.end()) {
        return false;
    }

    pl->trackIds.push_back(mediaId); // O(1) amortized
    return true;
}

bool PlaylistManager::removeTrack(const std::string& playlistId, const std::string& mediaId) {
    Playlist* pl = findById(playlistId);
    if (!pl) return false;

    auto it = std::find(pl->trackIds.begin(), pl->trackIds.end(), mediaId);
    if (it != pl->trackIds.end()) {
        pl->trackIds.erase(it); // O(n) element shift
        return true;
    }
    return false;
}

bool PlaylistManager::reorderTrack(const std::string& playlistId, const std::string& mediaId, int toIndex) {
    Playlist* pl = findById(playlistId);
    if (!pl) return false;

    auto it = std::find(pl->trackIds.begin(), pl->trackIds.end(), mediaId);
    if (it == pl->trackIds.end()) return false;

    // Remove from old position and insert at new index
    pl->trackIds.erase(it);
    int target = std::max(0, std::min(static_cast<int>(pl->trackIds.size()), toIndex));
    pl->trackIds.insert(pl->trackIds.begin() + target, mediaId);
    return true;
}

bool PlaylistManager::setTracks(const std::string& playlistId, const std::vector<std::string>& trackIds) {
    Playlist* pl = findById(playlistId);
    if (!pl) return false;
    pl->trackIds = trackIds;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// §2  Persistence and Serialization
// ─────────────────────────────────────────────────────────────────────────────

void PlaylistManager::saveToFile(const std::string& path) const {
    std::ofstream out(path);
    if (!out.is_open()) return;

    for (const auto& pl : playlists_) {
        out << pl.id << "|" << pl.name << "|";
        for (size_t i = 0; i < pl.trackIds.size(); ++i) {
            out << pl.trackIds[i] << (i + 1 < pl.trackIds.size() ? "," : "");
        }
        out << "\n";
    }
}

void PlaylistManager::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return;

    playlists_.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string id, name, tracksStr;

        if (std::getline(ss, id, '|') &&
            std::getline(ss, name, '|') &&
            std::getline(ss, tracksStr)) {

            Playlist pl;
            pl.id = id;
            pl.name = name;

            std::stringstream tss(tracksStr);
            std::string tid;
            while (std::getline(tss, tid, ',')) {
                if (!tid.empty()) pl.trackIds.push_back(tid);
            }
            playlists_.push_back(pl);
        }
    }
}

std::string PlaylistManager::toJson() const {
    std::string json = "[";
    for (size_t i = 0; i < playlists_.size(); ++i) {
        const auto& pl = playlists_[i];
        json += "{\"id\":\"" + pl.id + "\",\"name\":\"" + pl.name + "\",\"items\":[";
        for (size_t j = 0; j < pl.trackIds.size(); ++j) {
            json += "\"" + pl.trackIds[j] + "\"" + (j + 1 < pl.trackIds.size() ? "," : "");
        }
        json += std::string("]}") + (i + 1 < playlists_.size() ? "," : "");
    }
    json += "]";
    return json;
}

void PlaylistManager::fromJson(const std::string& json) {
    // Basic parser for incoming playlist arrays from JS
    // Expected structure: [{"id":"...","name":"...","items":["..."]}, ...]
    if (json.empty() || json[0] != '[') return;

    // Reset current lists if valid array received
    playlists_.clear();
    size_t pos = 0;
    while ((pos = json.find("{\"id\":\"", pos)) != std::string::npos) {
        pos += 7; // skip {"id":"
        size_t idEnd = json.find('"', pos);
        if (idEnd == std::string::npos) break;
        std::string id = json.substr(pos, idEnd - pos);

        size_t namePos = json.find("\"name\":\"", idEnd);
        if (namePos == std::string::npos) break;
        namePos += 8;
        size_t nameEnd = json.find('"', namePos);
        if (nameEnd == std::string::npos) break;
        std::string name = json.substr(namePos, nameEnd - namePos);

        Playlist pl;
        pl.id = id;
        pl.name = name;

        // Parse items array
        size_t itemsPos = json.find("\"items\":[", nameEnd);
        if (itemsPos != std::string::npos) {
            itemsPos += 9;
            size_t itemsEnd = json.find(']', itemsPos);
            if (itemsEnd != std::string::npos) {
                std::string itemsBlock = json.substr(itemsPos, itemsEnd - itemsPos);
                size_t q1 = 0;
                while ((q1 = itemsBlock.find('"', q1)) != std::string::npos) {
                    size_t q2 = itemsBlock.find('"', q1 + 1);
                    if (q2 == std::string::npos) break;
                    pl.trackIds.push_back(itemsBlock.substr(q1 + 1, q2 - q1 - 1));
                    q1 = q2 + 1;
                }
            }
        }
        playlists_.push_back(pl);
        pos = nameEnd;
    }
}

} // namespace mediax
