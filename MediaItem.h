#pragma once
/*
 * MediaItem — Shared data structure representing one media file (audio or video).
 *
 * Author: Adhiraj Singh
 *
 * Every field in this struct is populated by MetadataReader and consumed by
 * the Engine, Playlist, and Bridge layers. Keeping it as a clean struct
 * means it can cross module boundaries without coupling.
 */

#include <string>
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace mediax {

/// Discriminates between audio-only and audio+video content.
enum class MediaKind {
    AUDIO,  ///< MP3, AAC, OGG, FLAC, WAV, M4A …
    VIDEO   ///< MP4, M4V, MKV, AVI, MOV …
};

/**
 * @brief Plain-old-data bundle for one media file's metadata.
 */
struct MediaItem {
    // ── Identity ──────────────────────────────────────────────────────────
    std::string id;          ///< Unique ID (e.g., "lib-1", "hl-1")
    std::string title;       ///< Track title (from tag or filename stem)
    std::string artist;      ///< Primary artist / performer
    std::string album;       ///< Album or collection name
    std::string description; ///< Optional free-text description

    // ── Classification ────────────────────────────────────────────────────
    MediaKind   kind   = MediaKind::AUDIO; ///< AUDIO or VIDEO
    std::string format;                    ///< File format string, e.g. "MP3", "MP4", "FLAC"

    // ── Timing ────────────────────────────────────────────────────────────
    double      durationSecs = 0.0; ///< Total playback duration in seconds

    // ── Video-specific ────────────────────────────────────────────────────
    int         resolutionW = 0; ///< Video width in pixels (0 for audio-only)
    int         resolutionH = 0; ///< Video height in pixels (0 for audio-only)

    // ── File info ─────────────────────────────────────────────────────────
    long long   fileSizeBytes = 0LL; ///< Raw file size in bytes
    std::string bitrate;             ///< Human-readable bitrate, e.g. "320 kbps"
    std::string filePath;            ///< Absolute path to the source file on disk

    // ── Artwork ───────────────────────────────────────────────────────────
    std::string artDataUri;          ///< Base-64 data URI for embedded cover art

    // ── Statistics & Queue Sorting ────────────────────────────────────────
    int         playCount = 0;       ///< Historical play count for Smart Queue priority
    long long   addedTimestamp = 0;  ///< Unix timestamp when added to library
};

// ── Formatting Utilities ──────────────────────────────────────────────────────
inline std::string formatDuration(double secs) {
    long long totalSecs = static_cast<long long>(secs);
    long long minutes = totalSecs / 60;
    long long seconds = totalSecs % 60;
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << minutes << ":"
       << std::setfill('0') << std::setw(2) << seconds;
    return ss.str();
}

inline std::string formatFileSize(long long bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << kb << " KB";
        return ss.str();
    }
    double mb = kb / 1024.0;
    if (mb < 1024.0) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << mb << " MB";
        return ss.str();
    }
    double gb = mb / 1024.0;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << gb << " GB";
    return ss.str();
}

} // namespace mediax
