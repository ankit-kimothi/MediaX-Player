/**
 * @file Engine.cpp
 * @brief Central Engine singleton implementation for MediaX.
 *
 * OOP Concepts:
 *  - Singleton Pattern: Guarantees a single unified state and audio controller for the entire application.
 *  - Composition: Encapsulates SmartQueue, PlaylistManager, SearchEngine, MetadataReader, and AudioHandle.
 *  - Observer Pattern: Dispatches asynchronous state updates and event notifications to the JavaScript front-end.
 *
 * Author: Ankit Kimothi
 */

#include "Engine.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace mediax {

Engine& Engine::instance() {
    static Engine s_instance;
    return s_instance;
}

Engine::Engine() {
    // Initialise default playback state matching the web app
    state_.volume = 75.0;
    state_.speed = 1.0;
    state_.muted = false;
    state_.isPlaying = false;
    state_.position = 0.0;
    state_.shuffle = false;
    state_.repeat = false;
    state_.eqOn = true;
    state_.eqPreset = "flat";
    for (int i = 0; i < 6; ++i) state_.eqGains[i] = 0.0;
    state_.sourceType = "smart";
    state_.sourceId = "all";
}

void Engine::setEventCallback(std::function<void(const std::string&)> cb) {
    eventCb_ = cb;
}

Engine::PlaybackState Engine::getState() const {
    return state_;
}

const std::vector<MediaItem>& Engine::getLibrary() const {
    return library_;
}

MediaItem* Engine::findMedia(const std::string& id) {
    // Linear scan of library_: O(n)
    for (auto& item : library_) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

bool Engine::importFile(const std::string& filePath) {
    if (!MetadataReader::isSupported(filePath)) {
        return false;
    }

    // Extract file metadata using binary parser
    MediaItem item = MetadataReader::instance().extract(filePath);
    item.id = "lib-" + std::to_string(nextLibId_++);
    item.addedTimestamp = static_cast<long long>(std::time(nullptr));

    library_.push_back(item);

    // Update SearchEngine index with the new track
    SearchEngine::instance().buildIndex(library_);

    // Update SmartQueue library
    std::vector<std::string> allIds;
    std::map<std::string, int> playCounts;
    for (const auto& m : library_) {
        allIds.push_back(m.id);
        playCounts[m.id] = m.playCount;
    }
    SmartQueue::instance().loadLibrary(allIds, playCounts);

    // If no media is currently loaded, set this as the default
    if (state_.currentId.empty()) {
        state_.currentId = item.id;
        currentHandle_ = std::make_unique<AudioHandle>(item.filePath);
    }

    return true;
}

void Engine::handleCommand(const Command& cmd) {
    switch (cmd.type) {
        // ── Playback Controls ────────────────────────────────────────────────
        case CommandType::PLAY: {
            if (!cmd.id.empty() && cmd.id != state_.currentId) {
                state_.currentId = cmd.id;
                state_.position = 0.0;
                MediaItem* m = findMedia(cmd.id);
                if (m) {
                    m->playCount++;
                    currentHandle_ = std::make_unique<AudioHandle>(m->filePath);
                }
            }
            state_.isPlaying = true;
            SmartQueue::instance().setCurrentTrack(state_.currentId);
            pushStateSync();
            break;
        }

        case CommandType::PAUSE:
            state_.isPlaying = false;
            pushStateSync();
            break;

        case CommandType::TOGGLE:
            state_.isPlaying = !state_.isPlaying;
            if (state_.isPlaying && !state_.currentId.empty()) {
                SmartQueue::instance().setCurrentTrack(state_.currentId);
            }
            pushStateSync();
            break;

        case CommandType::STOP:
            state_.isPlaying = false;
            state_.position = 0.0;
            pushStateSync();
            break;

        case CommandType::SEEK:
            state_.position = std::max(0.0, cmd.numValue);
            pushStateSync();
            break;

        case CommandType::SKIP:
            state_.position = std::max(0.0, state_.position + cmd.numValue);
            pushStateSync();
            break;

        case CommandType::NEXT: {
            std::string nextId = SmartQueue::instance().nextTrack(state_.shuffle);
            if (!nextId.empty()) {
                state_.currentId = nextId;
                state_.position = 0.0;
                MediaItem* m = findMedia(nextId);
                if (m) {
                    m->playCount++;
                    currentHandle_ = std::make_unique<AudioHandle>(m->filePath);
                }
            }
            pushStateSync();
            break;
        }

        case CommandType::PREV: {
            std::string prevId = SmartQueue::instance().prevTrack();
            if (!prevId.empty()) {
                state_.currentId = prevId;
                state_.position = 0.0;
                MediaItem* m = findMedia(prevId);
                if (m) {
                    currentHandle_ = std::make_unique<AudioHandle>(m->filePath);
                }
            }
            pushStateSync();
            break;
        }

        // ── Audio Settings ───────────────────────────────────────────────────
        case CommandType::SET_VOLUME:
            state_.volume = std::max(0.0, std::min(100.0, cmd.numValue));
            state_.muted = (state_.volume == 0.0);
            pushStateSync();
            break;

        case CommandType::TOGGLE_MUTE:
            state_.muted = !state_.muted;
            pushStateSync();
            break;

        case CommandType::SET_SPEED:
            if (cmd.numValue > 0.1 && cmd.numValue <= 4.0) {
                state_.speed = cmd.numValue;
            }
            pushStateSync();
            break;

        case CommandType::TOGGLE_SHUFFLE:
            state_.shuffle = !state_.shuffle;
            SmartQueue::instance().setShuffle(state_.shuffle);
            pushStateSync();
            break;

        case CommandType::TOGGLE_REPEAT:
            state_.repeat = !state_.repeat;
            SmartQueue::instance().setRepeat(state_.repeat);
            pushStateSync();
            break;

        // ── Search & Source ──────────────────────────────────────────────────
        case CommandType::SELECT_SOURCE:
            state_.sourceType = cmd.value.empty() ? "smart" : cmd.value;
            state_.sourceId = cmd.id.empty() ? "all" : cmd.id;
            pushStateSync();
            break;

        case CommandType::SET_SEARCH: {
            state_.search = cmd.value;
            auto matchIds = SearchEngine::instance().search(cmd.value);
            std::string json = "[";
            for (size_t i = 0; i < matchIds.size(); ++i) {
                json += "\"" + matchIds[i] + "\"" + (i + 1 < matchIds.size() ? "," : "");
            }
            json += "]";
            pushEvent("SEARCH_RESULTS", json);
            break;
        }

        // ── Equalizer Controls ───────────────────────────────────────────────
        case CommandType::SET_EQ_PRESET:
            state_.eqPreset = cmd.value.empty() ? cmd.name : cmd.value;
            if (currentHandle_) {
                currentHandle_->applyPreset(state_.eqPreset);
                auto g = currentHandle_->getGains();
                for (int i = 0; i < 6 && i < static_cast<int>(g.size()); ++i) {
                    state_.eqGains[i] = g[i];
                }
            }
            pushStateSync();
            break;

        case CommandType::SET_EQ_BAND:
            if (cmd.index >= 0 && cmd.index < 6) {
                state_.eqGains[cmd.index] = cmd.numValue;
                state_.eqPreset = "custom";
                if (currentHandle_) {
                    currentHandle_->setGain(cmd.index, cmd.numValue);
                }
            }
            pushStateSync();
            break;

        case CommandType::TOGGLE_EQ:
            state_.eqOn = !state_.eqOn;
            if (currentHandle_) {
                currentHandle_->setEnabled(state_.eqOn);
            }
            pushStateSync();
            break;

        // ── Playlist Management (CRUD) ───────────────────────────────────────
        case CommandType::CREATE_PLAYLIST:
            PlaylistManager::instance().createPlaylist(cmd.name);
            pushEvent("PLAYLISTS_CHANGED", playlistsToJson());
            break;

        case CommandType::RENAME_PLAYLIST:
            PlaylistManager::instance().renamePlaylist(cmd.id, cmd.name);
            pushEvent("PLAYLISTS_CHANGED", playlistsToJson());
            break;

        case CommandType::DUPLICATE_PLAYLIST:
            PlaylistManager::instance().duplicatePlaylist(cmd.id);
            pushEvent("PLAYLISTS_CHANGED", playlistsToJson());
            break;

        case CommandType::DELETE_PLAYLIST:
            PlaylistManager::instance().deletePlaylist(cmd.id);
            pushEvent("PLAYLISTS_CHANGED", playlistsToJson());
            break;

        case CommandType::ADD_TO_PLAYLIST:
            if (!cmd.ids.empty()) {
                for (const auto& tid : cmd.ids) {
                    PlaylistManager::instance().addTrack(cmd.id, tid);
                }
            } else if (!cmd.value.empty()) {
                PlaylistManager::instance().addTrack(cmd.id, cmd.value);
            }
            pushEvent("PLAYLISTS_CHANGED", playlistsToJson());
            break;

        case CommandType::REMOVE_FROM_PLAYLIST:
            if (!cmd.ids.empty()) {
                for (const auto& tid : cmd.ids) {
                    PlaylistManager::instance().removeTrack(cmd.id, tid);
                }
            } else if (!cmd.value.empty()) {
                PlaylistManager::instance().removeTrack(cmd.id, cmd.value);
            }
            pushEvent("PLAYLISTS_CHANGED", playlistsToJson());
            break;

        case CommandType::REORDER_PLAYLIST:
            PlaylistManager::instance().reorderTrack(cmd.id, cmd.value, static_cast<int>(cmd.numValue));
            pushEvent("PLAYLISTS_CHANGED", playlistsToJson());
            break;

        case CommandType::SET_PLAYLIST_TRACKS:
            PlaylistManager::instance().setTracks(cmd.id, cmd.ids);
            pushEvent("PLAYLISTS_CHANGED", playlistsToJson());
            break;

        case CommandType::IMPORT_FILES:
            for (const auto& path : cmd.ids) {
                importFile(path);
            }
            pushEvent("LIBRARY_CHANGED", libraryToJson());
            break;

        case CommandType::TICK:
            pushStateSync();
            break;

        default:
            break;
    }
}

void Engine::tick(double deltaSeconds) {
    if (!state_.isPlaying) return;

    state_.position += deltaSeconds * state_.speed;

    double dur = 0.0;
    if (MediaItem* m = findMedia(state_.currentId)) {
        dur = m->durationSecs;
    }

    if (dur > 0.0 && state_.position >= dur) {
        SmartQueue::instance().onTrackFinished(state_.currentId);
        if (state_.repeat) {
            state_.position = 0.0;
        } else {
            Command nextCmd;
            nextCmd.type = CommandType::NEXT;
            handleCommand(nextCmd);
            return;
        }
    }

    // Push lightweight TICK update to JS: fires ~100 ms
    std::string tickPayload = "{\"position\":" + std::to_string(state_.position) + "}";
    pushEvent("TICK", tickPayload);
}

// ─────────────────────────────────────────────────────────────────────────────
// §2  JSON Serialization Helpers
// ─────────────────────────────────────────────────────────────────────────────

void Engine::pushEvent(const std::string& type, const std::string& payload) {
    if (!eventCb_) return;
    std::string jsCall = "dispatch({ type: \"" + type + "\", payload: " + payload + " });";
    eventCb_(jsCall);
}

void Engine::pushStateSync() {
    pushEvent("SYNC", stateToJson());
}

std::string Engine::escapeJson(const std::string& s) const {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b";  break;
            case '\f': o << "\\f";  break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1f) {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

std::string Engine::stateToJson() const {
    std::string json = "{";
    json += "\"currentId\":\"" + escapeJson(state_.currentId) + "\",";
    json += "\"isPlaying\":" + std::string(state_.isPlaying ? "true" : "false") + ",";
    json += "\"position\":" + std::to_string(state_.position) + ",";
    json += "\"volume\":" + std::to_string(state_.volume) + ",";
    json += "\"muted\":" + std::string(state_.muted ? "true" : "false") + ",";
    json += "\"speed\":" + std::to_string(state_.speed) + ",";
    json += "\"shuffle\":" + std::string(state_.shuffle ? "true" : "false") + ",";
    json += "\"repeat\":" + std::string(state_.repeat ? "true" : "false") + ",";
    json += "\"eqOn\":" + std::string(state_.eqOn ? "true" : "false") + ",";
    json += "\"eqPreset\":\"" + escapeJson(state_.eqPreset) + "\",";
    json += "\"eqGains\":[";
    for (int i = 0; i < 6; ++i) {
        json += std::to_string(state_.eqGains[i]) + (i < 5 ? "," : "");
    }
    json += "],";
    json += "\"sourceType\":\"" + escapeJson(state_.sourceType) + "\",";
    json += "\"sourceId\":\"" + escapeJson(state_.sourceId) + "\",";
    json += "\"search\":\"" + escapeJson(state_.search) + "\"";
    json += "}";
    return json;
}

std::string Engine::mediaItemToJson(const MediaItem& item) const {
    std::string json = "{";
    json += "\"id\":\"" + escapeJson(item.id) + "\",";
    json += "\"title\":\"" + escapeJson(item.title) + "\",";
    json += "\"artist\":\"" + escapeJson(item.artist) + "\",";
    json += "\"album\":\"" + escapeJson(item.album) + "\",";
    json += "\"kind\":\"" + std::string(item.kind == MediaKind::VIDEO ? "video" : "audio") + "\",";
    json += "\"format\":\"" + escapeJson(item.format) + "\",";
    json += "\"duration\":" + std::to_string(item.durationSecs) + ",";
    json += "\"resolution\":\"" + (item.resolutionW > 0 ? (std::to_string(item.resolutionW) + " x " + std::to_string(item.resolutionH)) : "—") + "\",";
    json += "\"size\":\"" + formatFileSize(item.fileSizeBytes) + "\",";
    json += "\"bitrate\":\"" + escapeJson(item.bitrate) + "\",";
    json += "\"path\":\"" + escapeJson(item.filePath) + "\",";
    json += "\"art\":\"" + escapeJson(item.artDataUri) + "\",";
    json += "\"description\":\"" + escapeJson(item.description) + "\",";
    json += "\"playCount\":" + std::to_string(item.playCount);
    json += "}";
    return json;
}

std::string Engine::libraryToJson() const {
    std::string json = "[";
    for (size_t i = 0; i < library_.size(); ++i) {
        json += mediaItemToJson(library_[i]) + (i + 1 < library_.size() ? "," : "");
    }
    json += "]";
    return json;
}

std::string Engine::playlistsToJson() const {
    return PlaylistManager::instance().toJson();
}

} // namespace mediax
