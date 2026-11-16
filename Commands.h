// =============================================================================
// bridge/Commands.h — MediaX IPC Command & Event Protocol
// Author: Ankit Kimothi
//
// This header defines the full command/event protocol used between the
// JavaScript front-end and the C++ back-end.
//
// DATA FLOW OVERVIEW:
//
//   [JS front-end]                       [C++ back-end]
//       │                                      │
//       │  window.chrome.webview               │
//       │    .postMessage(jsonString)  ──────► │  Bridge::handleFromJS(json)
//       │                                      │      │
//       │                                      │  parseCommand(json)
//       │                                      │      │
//       │                                      │  Engine::handleCommand(cmd)
//       │                                      │
//       │  window.mediax.onEvent(event) ◄───── │  Bridge::pushEventToJS(event)
//       │                                      │  eventToJson(event)
//
// WIRE FORMAT (JSON sent from JS → C++):
//   {
//     "type":     "PLAY" | "PAUSE" | …  (required, maps to CommandType),
//     "id":       "track-uuid",           (optional),
//     "name":     "My Playlist",          (optional),
//     "value":    "some string",          (optional),
//     "numValue": 0.75,                   (optional, e.g. volume, seek pos),
//     "index":    2,                      (optional, e.g. playlist position),
//     "ids":      ["id1","id2"]           (optional, track id list)
//   }
//
// WIRE FORMAT (JSON sent from C++ → JS):
//   { "type": "TICK", "payload": "{...}" }
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <cstdlib>   // std::strtod
#include <algorithm> // std::find

namespace mediax {

// ===========================================================================
// CommandType — All commands the JS front-end can send to the C++ engine
// ===========================================================================
/**
 * @brief Enumerates every command the JavaScript UI may dispatch to the engine.
 *
 * Keep this list in sync with the commandType constants used in app.js.
 * When adding a new command, also add its string mapping in parseCommand().
 */
enum class CommandType
{
    // ---- Playback -------------------------------------------------------
    PLAY,             ///< Begin (or resume) playback of the current track
    PAUSE,            ///< Pause playback
    TOGGLE,           ///< Toggle between play and pause
    STOP,             ///< Stop playback and reset position to 0
    SEEK,             ///< Seek to position (numValue = seconds from start)
    SKIP,             ///< Skip forward/backward (numValue = delta seconds)
    NEXT,             ///< Advance to the next track in the queue
    PREV,             ///< Go back to the previous track (or start of current)

    // ---- State tick -----------------------------------------------------
    TICK,             ///< Periodic heartbeat — JS requests current playback state

    // ---- Audio controls -------------------------------------------------
    SET_VOLUME,       ///< Set volume level (numValue = 0.0 – 1.0)
    TOGGLE_MUTE,      ///< Toggle the mute flag
    SET_SPEED,        ///< Set playback speed (numValue = 0.5, 1.0, 1.5, 2.0 …)

    // ---- Queue / shuffle / repeat ---------------------------------------
    TOGGLE_SHUFFLE,   ///< Toggle shuffle mode on/off
    TOGGLE_REPEAT,    ///< Cycle repeat mode: OFF → ONE → ALL → OFF

    // ---- Source selection -----------------------------------------------
    SELECT_SOURCE,    ///< Select a track or source (id = media item id)

    // ---- Playlist management --------------------------------------------
    REORDER_PLAYLIST, ///< Move a track within the playlist (index = new pos)
    SET_SEARCH,       ///< Update the search/filter query (value = query string)

    // ---- Equaliser ------------------------------------------------------
    SET_EQ_PRESET,    ///< Apply a named EQ preset (value = preset name)
    SET_EQ_BAND,      ///< Adjust a single EQ band (index = band, numValue = dB)
    TOGGLE_EQ,        ///< Enable / disable the equaliser

    // ---- Playlist CRUD --------------------------------------------------
    CREATE_PLAYLIST,      ///< Create a new empty playlist (name = playlist name)
    RENAME_PLAYLIST,      ///< Rename a playlist (id = playlist id, name = new name)
    DUPLICATE_PLAYLIST,   ///< Clone a playlist (id = source playlist id)
    DELETE_PLAYLIST,      ///< Delete a playlist (id = playlist id)
    SET_PLAYLIST_TRACKS,  ///< Replace a playlist's track list (id = playlist, ids = track list)
    ADD_TO_PLAYLIST,      ///< Add tracks to a playlist (id = playlist, ids = tracks)
    REMOVE_FROM_PLAYLIST, ///< Remove tracks from a playlist (id = playlist, ids = tracks)

    // ---- Library import -------------------------------------------------
    IMPORT_FILES,     ///< Import media files into the library (ids = file paths)

    // ---- Fallback -------------------------------------------------------
    UNKNOWN           ///< Unrecognised command — engine should log and ignore
};

// ===========================================================================
// Command — Parsed representation of a JSON message from the JS front-end
// ===========================================================================
/**
 * @brief Holds the decoded fields of a command message from JavaScript.
 *
 * Not every field is populated for every command.  Unused fields retain
 * their default values.  The Engine should check CommandType first and then
 * read only the fields relevant to that command.
 */
struct Command
{
    CommandType              type     = CommandType::UNKNOWN; ///< Which action to perform
    std::string              id;       ///< Primary entity id (track, playlist, …)
    std::string              name;     ///< Human-readable name (e.g. new playlist name)
    std::string              value;    ///< String payload (search query, preset name, …)
    double                   numValue = 0.0; ///< Numeric payload (volume, seek pos, …)
    int                      index    = 0;   ///< Integer payload (band index, position, …)
    std::vector<std::string> ids;      ///< List of ids (track list, file paths, …)
};

// ===========================================================================
// Event — An event the C++ engine pushes to the JavaScript front-end
// ===========================================================================
/**
 * @brief Represents an event serialised as JSON and sent to the JS layer.
 *
 * The Engine constructs an Event, calls Bridge::pushEventToJS(), which
 * serialises it via eventToJson() and runs:
 *   window.mediax && window.mediax.onEvent(<json>)
 * inside WebView2 via ExecuteScript().
 */
struct Event
{
    std::string type;    ///< Event type string (e.g. "TICK", "STATE_UPDATE")
    std::string payload; ///< JSON-encoded payload (engine-specific schema)
};

// ===========================================================================
// parseCommand — Hand-rolled JSON → Command parser
// ===========================================================================
/**
 * @brief Parses a JSON object string into a Command struct.
 *
 * Uses only std::string::find and substr — no external JSON library needed.
 * This is intentionally simple; it assumes the JSON is well-formed (as
 * produced by the JS front-end) and does not handle deeply nested objects.
 *
 * String fields are extracted by scanning for   "key":"value"
 * Numeric fields are extracted by scanning for  "key":NUMBER
 * Array of strings is extracted for the "ids" key.
 *
 * @param json  A UTF-8 JSON string, e.g. {"type":"PLAY","id":"abc123"}
 * @return A Command with all recognised fields populated.
 *
 * @note On parse failure (key not found) the field retains its default value.
 *       CommandType::UNKNOWN is returned when the "type" value is not in the
 *       known set, so the Engine can log and discard it safely.
 */
inline Command parseCommand(const std::string& json)
{
    Command cmd;

    // -----------------------------------------------------------------------
    // Helper: extract a quoted string value for a given key
    //   e.g. extractString(json, "id") returns "abc123" for  "id":"abc123"
    // -----------------------------------------------------------------------
    auto extractString = [&](const std::string& key) -> std::string
    {
        // Build the search token:  "key":"
        std::string token = "\"" + key + "\":\"";
        auto pos = json.find(token);
        if (pos == std::string::npos) return {};
        pos += token.size(); // skip past  "key":"

        // Find the closing quote, handling a simple \" escape
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos; // skip backslash
                // Handle common JSON escape sequences
                switch (json[pos]) {
                    case '"':  result += '"';  break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/';  break;
                    case 'n':  result += '\n'; break;
                    case 't':  result += '\t'; break;
                    default:   result += json[pos]; break;
                }
            } else {
                result += json[pos];
            }
            ++pos;
        }
        return result;
    };

    // -----------------------------------------------------------------------
    // Helper: extract a numeric value (double) for a given key
    //   e.g. extractDouble(json, "numValue") returns 0.75 for "numValue":0.75
    // -----------------------------------------------------------------------
    auto extractDouble = [&](const std::string& key) -> double
    {
        std::string token = "\"" + key + "\":";
        auto pos = json.find(token);
        if (pos == std::string::npos) return 0.0;
        pos += token.size();

        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
            ++pos;

        // Parse the number using strtod
        const char* start = json.c_str() + pos;
        char* end = nullptr;
        double val = std::strtod(start, &end);
        if (start == end) return 0.0; // no valid number found
        return val;
    };

    // -----------------------------------------------------------------------
    // Helper: extract a JSON array of strings for a given key
    //   e.g. extractStringArray(json, "ids") returns {"id1","id2"} for
    //        "ids":["id1","id2"]
    // -----------------------------------------------------------------------
    auto extractStringArray = [&](const std::string& key) -> std::vector<std::string>
    {
        std::vector<std::string> result;
        std::string token = "\"" + key + "\":[";
        auto pos = json.find(token);
        if (pos == std::string::npos) return result;
        pos += token.size();

        // Iterate over the array until we hit ']'
        while (pos < json.size() && json[pos] != ']') {
            if (json[pos] == '"') {
                ++pos; // skip opening quote
                std::string item;
                while (pos < json.size() && json[pos] != '"') {
                    if (json[pos] == '\\' && pos + 1 < json.size()) {
                        ++pos;
                        item += json[pos];
                    } else {
                        item += json[pos];
                    }
                    ++pos;
                }
                result.push_back(std::move(item));
                if (pos < json.size()) ++pos; // skip closing quote
            } else {
                ++pos; // skip comma, whitespace, etc.
            }
        }
        return result;
    };

    // -----------------------------------------------------------------------
    // Extract all fields
    // -----------------------------------------------------------------------
    std::string typeStr = extractString("type");
    cmd.id       = extractString("id");
    cmd.name     = extractString("name");
    cmd.value    = extractString("value");
    cmd.numValue = extractDouble("numValue");
    cmd.index    = static_cast<int>(extractDouble("index"));
    cmd.ids      = extractStringArray("ids");

    // -----------------------------------------------------------------------
    // Map the type string → CommandType enum
    // -----------------------------------------------------------------------
    if      (typeStr == "PLAY")                 cmd.type = CommandType::PLAY;
    else if (typeStr == "PAUSE")                cmd.type = CommandType::PAUSE;
    else if (typeStr == "TOGGLE")               cmd.type = CommandType::TOGGLE;
    else if (typeStr == "STOP")                 cmd.type = CommandType::STOP;
    else if (typeStr == "SEEK")                 cmd.type = CommandType::SEEK;
    else if (typeStr == "SKIP")                 cmd.type = CommandType::SKIP;
    else if (typeStr == "NEXT")                 cmd.type = CommandType::NEXT;
    else if (typeStr == "PREV")                 cmd.type = CommandType::PREV;
    else if (typeStr == "TICK")                 cmd.type = CommandType::TICK;
    else if (typeStr == "SET_VOLUME")           cmd.type = CommandType::SET_VOLUME;
    else if (typeStr == "TOGGLE_MUTE")          cmd.type = CommandType::TOGGLE_MUTE;
    else if (typeStr == "SET_SPEED")            cmd.type = CommandType::SET_SPEED;
    else if (typeStr == "TOGGLE_SHUFFLE")       cmd.type = CommandType::TOGGLE_SHUFFLE;
    else if (typeStr == "TOGGLE_REPEAT")        cmd.type = CommandType::TOGGLE_REPEAT;
    else if (typeStr == "SELECT_SOURCE")        cmd.type = CommandType::SELECT_SOURCE;
    else if (typeStr == "REORDER_PLAYLIST")     cmd.type = CommandType::REORDER_PLAYLIST;
    else if (typeStr == "SET_SEARCH")           cmd.type = CommandType::SET_SEARCH;
    else if (typeStr == "SET_EQ_PRESET")        cmd.type = CommandType::SET_EQ_PRESET;
    else if (typeStr == "SET_EQ_BAND")          cmd.type = CommandType::SET_EQ_BAND;
    else if (typeStr == "TOGGLE_EQ")            cmd.type = CommandType::TOGGLE_EQ;
    else if (typeStr == "CREATE_PLAYLIST")      cmd.type = CommandType::CREATE_PLAYLIST;
    else if (typeStr == "RENAME_PLAYLIST")      cmd.type = CommandType::RENAME_PLAYLIST;
    else if (typeStr == "DUPLICATE_PLAYLIST")   cmd.type = CommandType::DUPLICATE_PLAYLIST;
    else if (typeStr == "DELETE_PLAYLIST")      cmd.type = CommandType::DELETE_PLAYLIST;
    else if (typeStr == "SET_PLAYLIST_TRACKS")  cmd.type = CommandType::SET_PLAYLIST_TRACKS;
    else if (typeStr == "ADD_TO_PLAYLIST")      cmd.type = CommandType::ADD_TO_PLAYLIST;
    else if (typeStr == "REMOVE_FROM_PLAYLIST") cmd.type = CommandType::REMOVE_FROM_PLAYLIST;
    else if (typeStr == "IMPORT_FILES")         cmd.type = CommandType::IMPORT_FILES;
    else                                        cmd.type = CommandType::UNKNOWN;

    return cmd;
}

// ===========================================================================
// eventToJson — Serialise an Event struct to a JSON string
// ===========================================================================
/**
 * @brief Converts an Event to a JSON string for injection into WebView2.
 *
 * Output format:
 *   {"type":"TICK","payload":{...inner json...}}
 *
 * The payload field is treated as a raw JSON value (not re-quoted) so the
 * Engine can embed a nested JSON object.  If you want payload to be a plain
 * string, the Engine must wrap it in escaped quotes itself.
 *
 * @param e  The event to serialise.
 * @return   A JSON string ready to be passed to ExecuteScript().
 *
 * @note No escaping is applied to e.type since it is always an internal
 *       constant.  e.payload is emitted verbatim; the Engine is responsible
 *       for ensuring it is valid JSON.
 */
inline std::string eventToJson(const Event& e)
{
    // Build:  {"type":"<type>","payload":<payload>}
    return std::string("{\"type\":\"") + e.type + "\",\"payload\":" + e.payload + "}";
}

} // namespace mediax
