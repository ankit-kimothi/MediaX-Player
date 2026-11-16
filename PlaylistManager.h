#pragma once
/**
 * @file PlaylistManager.h
 * @brief User playlist management, ordering, and file persistence.
 *
 * DSA Concepts:
 *  - Dynamic Array (std::vector): Vector-based storage for playlist collections and track orders.
 *  - Linear & Positional Search: O(n) element search, erase-remove idiom, and index-based repositioning.
 *  - Data Serialization: File I/O for saving and loading playlists across user sessions.
 *
 * Author: Yatharth Pant
 */

#include <string>
#include <vector>

namespace mediax {

struct Playlist {
    std::string id;
    std::string name;
    std::vector<std::string> trackIds; ///< Ordered list of media IDs in playlist
};

class PlaylistManager {
public:
    static PlaylistManager& instance();

    // Persistence
    void loadFromFile(const std::string& path);
    void saveToFile(const std::string& path) const;

    // Queries
    const std::vector<Playlist>& getAll() const;
    Playlist* findById(const std::string& id);
    const Playlist* findById(const std::string& id) const;
    std::vector<std::string> getTrackIds(const std::string& playlistId) const;

    // CRUD operations
    Playlist& createPlaylist(const std::string& name);
    bool renamePlaylist(const std::string& id, const std::string& newName);
    bool deletePlaylist(const std::string& id);
    Playlist duplicatePlaylist(const std::string& id);

    // Track manipulations
    bool addTrack(const std::string& playlistId, const std::string& mediaId);
    bool removeTrack(const std::string& playlistId, const std::string& mediaId);
    bool reorderTrack(const std::string& playlistId, const std::string& mediaId, int toIndex);
    bool setTracks(const std::string& playlistId, const std::vector<std::string>& trackIds);

    // JSON serialization for bridge
    std::string toJson() const;
    void fromJson(const std::string& json);

private:
    PlaylistManager();
    std::vector<Playlist> playlists_;
    int nextId_;

    std::string generateId();
};

} // namespace mediax

