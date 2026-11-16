#pragma once
// =============================================================================
// engine/Engine.h — MediaX Central Engine Singleton
//
// OOP Concepts : Singleton  (guarantees exactly one Engine lives per process),
//                Composition (owns every subsystem as a member or unique_ptr),
//                Observer    (Bridge registers an event callback so the Engine
//                             can push state/events to JS without knowing about
//                             the WebView2 layer)
//
// Author: Ankit Kimothi
// =============================================================================

#include "MediaItem.h"          // Shared data struct for every media file
#include "SmartQueue.h"         // Playback queue with shuffle / repeat logic
#include "MetadataReader.h"     // Reads ID3 / mp4 / ogg metadata from disk
#include "PlaylistManager.h"    // CRUD operations on user playlists
#include "SearchEngine.h"       // Full-text search over the library
#include "AudioHandle.h"        // Wraps the platform audio / EQ pipeline

#include "../bridge/Commands.h" // Command & Event protocol shared with Bridge

#include <functional>  // std::function   — for the Observer callback
#include <vector>      // std::vector     — library of MediaItems
#include <string>      // std::string     — everywhere
#include <memory>      // std::unique_ptr — owns the live AudioHandle

namespace mediax {

// =============================================================================
// Engine
//
// Central singleton that owns all MediaX subsystems and routes every command
// that arrives from the JavaScript front-end (via Bridge → handleCommand).
//
// Design:
//   • The *only* way to reach Engine is through Engine::instance().
//   • The Bridge registers a JS executor via setEventCallback().
//     Engine calls that lambda whenever state changes (Observer pattern).
//   • Engine owns the SmartQueue, PlaylistManager, SearchEngine and
//     MetadataReader singletons by usage (they each live as singletons
//     themselves), and exclusively owns the live AudioHandle via unique_ptr.
// =============================================================================
class Engine
{
public:
    // -------------------------------------------------------------------------
    // instance() — Meyers-style singleton accessor
    //
    // Returns the single Engine instance.  Constructed the first time this
    // function is called; destroyed when the process exits.
    // Thread-safety: C++11 guarantees static-local initialisation is atomic.
    // -------------------------------------------------------------------------
    static Engine& instance();

    // -------------------------------------------------------------------------
    // handleCommand — Main entry point for every JS → C++ action
    //
    // The Bridge calls this once per message received from the WebView2 layer.
    // Internally this is a large switch-dispatch that mutates PlaybackState and
    // calls the appropriate subsystem(s).
    //
    // @param cmd  Parsed Command struct (from parseCommand in Commands.h).
    // -------------------------------------------------------------------------
    void handleCommand(const Command& cmd);

    // -------------------------------------------------------------------------
    // setEventCallback — Register the JS event pusher (Observer pattern)
    //
    // Bridge calls this once at startup, passing a lambda that wraps
    // WebView2's ExecuteScript.  Engine stores it as eventCb_ and calls it
    // every time state changes, so the JS front-end is kept in sync without
    // the Engine knowing anything about WebView2 itself.
    //
    // @param cb  A callable that accepts a JavaScript statement string and
    //            executes it inside the WebView2 context.
    // -------------------------------------------------------------------------
    void setEventCallback(std::function<void(const std::string&)> cb);

    // -------------------------------------------------------------------------
    // Library management
    // -------------------------------------------------------------------------

    /// Import a file from disk: read metadata, assign an id, add to library_.
    /// Returns true on success, false if the format is unsupported.
    bool importFile(const std::string& filePath);

    /// Returns a const reference to the full in-memory library.
    const std::vector<MediaItem>& getLibrary() const;

    /// Linear scan of library_ to find the item with the given id.
    /// Returns nullptr if not found.
    /// Complexity: O(n) — acceptable for a college-project library size.
    ///             (Could use std::unordered_map<string,MediaItem*> for O(1).)
    MediaItem* findMedia(const std::string& id);

    // -------------------------------------------------------------------------
    // PlaybackState — mirrors the JS `state` object exactly
    //
    // Every field here corresponds 1:1 to a property in app.js so that
    // stateToJson() can produce the exact JSON the JS `dispatch({type:"SYNC"})`
    // handler expects.
    // -------------------------------------------------------------------------
    struct PlaybackState
    {
        std::string currentId;          ///< id of the currently loaded track
        bool        isPlaying  = false; ///< true while audio is running
        double      position   = 0.0;   ///< playback cursor, seconds
        double      volume     = 75.0;  ///< master volume 0-100
        bool        muted      = false; ///< whether audio is muted
        double      speed      = 1.0;   ///< playback speed multiplier
        bool        shuffle    = false; ///< shuffle mode flag
        bool        repeat     = false; ///< repeat mode flag
        double      eqGains[6] = {};    ///< per-band EQ gain (dB), 6 bands
        bool        eqOn       = true;  ///< global EQ bypass flag
        std::string eqPreset   = "flat";///< active EQ preset name
        std::string sourceType = "smart"; ///< "smart" or "user" (playlist)
        std::string sourceId   = "all"; ///< active source id
        std::string search;             ///< current search/filter string
    };

    /// Returns a copy of the current PlaybackState.
    PlaybackState getState() const;

    // -------------------------------------------------------------------------
    // tick — Called by main's timer (~100 ms) to advance playback position
    //
    // Advances state_.position by deltaSeconds * state_.speed, checks for
    // end-of-track, and fires auto-next or repeat as appropriate.
    //
    // @param deltaSeconds  Time since the last tick, in seconds.
    // -------------------------------------------------------------------------
    void tick(double deltaSeconds);

private:
    // -------------------------------------------------------------------------
    // Constructor — private; only instance() may create the Engine.
    // -------------------------------------------------------------------------
    Engine();

    // Disable copy construction and copy assignment (Singleton rule).
    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    /// Build a JS dispatch call and fire it via eventCb_.
    /// @param type    Event type string (e.g. "SYNC", "TICK", "LIBRARY_CHANGED")
    /// @param payload Raw JSON value to embed as the payload field.
    void pushEvent(const std::string& type, const std::string& payload);

    /// Serialise state_ and push a SYNC event so JS can reconcile.
    void pushStateSync();

    /// Serialise the full PlaybackState to a JSON object string.
    std::string stateToJson() const;

    /// Serialise a single MediaItem to a JSON object string.
    std::string mediaItemToJson(const MediaItem& item) const;

    /// Serialise the entire library_ as a JSON array string.
    std::string libraryToJson() const;

    /// Serialise all playlists (from PlaylistManager) as a JSON array string.
    std::string playlistsToJson() const;

    /// Escape a plain string for safe embedding inside a JSON string value.
    /// Handles backslash, double-quote, and newline.
    std::string escapeJson(const std::string& s) const;

    // -------------------------------------------------------------------------
    // Data members
    // -------------------------------------------------------------------------
    std::vector<MediaItem>                   library_;        ///< All imported tracks
    PlaybackState                            state_;          ///< Live playback state
    std::unique_ptr<AudioHandle>             currentHandle_;  ///< Live audio pipeline
    std::function<void(const std::string&)>  eventCb_;        ///< Observer callback → JS
    int                                      nextLibId_ = 1;  ///< Auto-increment library id
};

} // namespace mediax
