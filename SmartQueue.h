/**
 * @file SmartQueue.h
 * @brief Smart Queue module for MediaX — doubly-linked-list history stack + max-heap priority queue.
 *
 * DSA Concepts:
 *  - Doubly Linked List (HistoryStack): O(1) push/pop at head, bi-directional cursor traversal
 *    for back/forward navigation through play history.
 *  - Max-Heap Priority Queue (PriorityQueue): O(log n) insert & extract-max for priority-based
 *    track scheduling; tracks played more often get higher priority in the auto-queue.
 *
 * Author: Ankit Kimothi
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <random>
#include <algorithm>
#include <climits>  // INT_MAX

// ─────────────────────────────────────────────────────────────────────────────
// §1  Node — element of the doubly-linked-list used by HistoryStack
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @struct Node
 * @brief A single node in the doubly-linked play-history list.
 *
 * Each node stores one media ID and bidirectional pointers so we can walk
 * the list in either direction (forward = newer, prev = older).
 */
struct Node {
    std::string mediaId;  ///< ID of the media item at this position
    Node*       prev;     ///< pointer to the older (previous) node
    Node*       next;     ///< pointer to the newer (next) node

    /**
     * @brief Construct a Node with the given media ID.
     * @param id  The media identifier to store in this node.
     */
    explicit Node(const std::string& id)
        : mediaId(id), prev(nullptr), next(nullptr) {}
};

// ─────────────────────────────────────────────────────────────────────────────
// §2  HistoryStack — doubly-linked list for navigable play history
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class HistoryStack
 * @brief Maintains a bounded doubly-linked play-history list with a movable cursor.
 *
 * The list grows at the **head** (most recent track). A cursor allows the user
 * to navigate back (toward older entries) and forward (toward newer entries).
 *
 * Complexity summary:
 *  - pushFront / popFront : O(1)
 *  - goBack / goForward   : O(1)
 *  - toVector             : O(n)
 *
 * Memory is bounded by MAX_SIZE: once the list exceeds MAX_SIZE nodes, the
 * oldest tail node is automatically removed.
 */
class HistoryStack {
public:
    // ── Constructor / Destructor ────────────────────────────────────────────

    HistoryStack();
    ~HistoryStack();

    // ── Mutation ────────────────────────────────────────────────────────────

    /**
     * @brief Insert a new entry at the front (most-recent end) of the list.
     *
     * Also resets the cursor to the new head so getCurrent() immediately
     * returns the newly pushed track.
     *
     * @param id  Media ID to push.
     */
    void pushFront(const std::string& id);

    /**
     * @brief Remove and return the front (most-recent) entry.
     *
     * @return The media ID that was at the front, or "" if the list was empty.
     */
    std::string popFront();

    // ── Cursor navigation ───────────────────────────────────────────────────

    /**
     * @brief Move the cursor toward an older (previous) entry.
     *
     * If the cursor is currently nullptr (meaning we are "in front of" the
     * list), it jumps to the tail (oldest entry).
     *
     * @return true if the cursor moved; false if already at the oldest entry.
     */
    bool goBack();

    /**
     * @brief Move the cursor toward a newer (next) entry.
     *
     * @return true if the cursor moved; false if already at the newest entry
     *         (i.e. cursor->next == nullptr or cursor == head_).
     */
    bool goForward();

    // ── Read-only accessors ─────────────────────────────────────────────────

    /**
     * @brief Peek at the front (most-recent) entry without removing it.
     * @return The front media ID, or "" if the list is empty.
     */
    std::string peekFront() const;

    /**
     * @brief Return the media ID currently under the cursor.
     * @return The cursor's media ID, or "" if the cursor is null / list empty.
     */
    std::string getCurrent() const;

    /**
     * @brief Collect all entries from head (newest) to tail (oldest).
     * @return Vector of media IDs in newest-first order.
     */
    std::vector<std::string> toVector() const;

    /**
     * @brief Number of entries currently in the history list.
     */
    size_t size() const;

private:
    Node*        head_;                  ///< most-recent entry
    Node*        tail_;                  ///< oldest entry
    Node*        cursor_;                ///< current navigation position
    size_t       size_;                  ///< current node count
    const size_t MAX_SIZE = 50;          ///< maximum history depth
};

// ─────────────────────────────────────────────────────────────────────────────
// §3  PQEntry — element stored in the priority-queue heap
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @struct PQEntry
 * @brief One entry in the max-heap priority queue.
 *
 * The @c operator< is defined so that std::make_heap / our manual heapify
 * treats *higher priority values* as the maximum — this is the standard
 * "reverse less-than" trick used by max-heaps implemented over std::vector.
 */
struct PQEntry {
    std::string mediaId;   ///< ID of the media item
    int         priority;  ///< scheduling priority (higher = sooner)

    /**
     * @brief Comparison used by the heap to establish max-heap ordering.
     *
     * We return `this->priority < other.priority` so that the entry with the
     * *largest* priority floats to the root (max-heap semantics).
     *
     * @param other  Entry to compare against.
     * @return true if *this* should be ordered **below** @p other.
     */
    bool operator<(const PQEntry& other) const {
        return priority < other.priority;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// §4  PriorityQueue — manual max-heap over std::vector<PQEntry>
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class PriorityQueue
 * @brief Max-heap priority queue for scheduling tracks.
 *
 * Internally backed by a std::vector<PQEntry> and maintained with custom
 * heapifyUp / heapifyDown routines so that the entry with the highest
 * priority is always at index 0.
 *
 * Complexity:
 *  - push  : O(log n)   — append then bubble up
 *  - pop   : O(log n)   — swap root with last, pop last, sift down
 *  - empty : O(1)
 */
class PriorityQueue {
public:
    // ── Mutation ────────────────────────────────────────────────────────────

    /**
     * @brief Insert a media item with the given priority into the heap.
     *
     * Appends the new PQEntry to the internal vector then restores the
     * heap property by bubbling the entry upward.
     *
     * @param id        Media ID to enqueue.
     * @param priority  Scheduling priority (higher value → plays sooner).
     */
    void push(const std::string& id, int priority);

    /**
     * @brief Remove and return the highest-priority media ID.
     *
     * Swaps the root with the last element, removes the last element, then
     * sifts the new root down to restore the heap property.
     *
     * @return The media ID that had the highest priority, or "" if empty.
     */
    std::string pop();

    // ── Read-only accessors ─────────────────────────────────────────────────

    /** @return true if the heap contains no entries. */
    bool empty() const;

    /** @return Number of entries in the heap. */
    size_t size() const;

private:
    std::vector<PQEntry> heap_;  ///< underlying storage for heap nodes

    /**
     * @brief Bubble the entry at index @p i upward until the heap property holds.
     *
     * Parent of node i is at index (i-1)/2. Swap while parent.priority < child.priority.
     *
     * @param i  Index of the newly inserted element.
     */
    void heapifyUp(int i);

    /**
     * @brief Sift the entry at index @p i downward until the heap property holds.
     *
     * Children of node i are at 2*i+1 (left) and 2*i+2 (right). Swap with
     * the larger child while that child's priority exceeds the current node's.
     *
     * @param i  Index of the element to sift down (usually 0 after a pop).
     */
    void heapifyDown(int i);
};

// ─────────────────────────────────────────────────────────────────────────────
// §5  SmartQueue — public interface (singleton)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class SmartQueue
 * @brief Singleton that ties together HistoryStack and PriorityQueue to power
 *        MediaX's smart playback scheduling.
 *
 * Usage flow:
 *  1. Call loadLibrary() once at startup with all known media IDs and their
 *     historical play-counts.
 *  2. Call setCurrentTrack() when the user manually selects a track.
 *  3. Call nextTrack(shuffle) / prevTrack() to advance or retreat.
 *  4. Call onTrackFinished() after each track ends so play-counts are updated.
 *  5. Use enqueueNext() to jump-queue a specific track at the front.
 */
class SmartQueue {
public:
    // ── Singleton access ────────────────────────────────────────────────────

    /**
     * @brief Get the single global SmartQueue instance (Meyer's singleton).
     */
    static SmartQueue& instance();

    // Prevent copy and move (singleton)
    SmartQueue(const SmartQueue&)            = delete;
    SmartQueue& operator=(const SmartQueue&) = delete;

    // ── Library management ──────────────────────────────────────────────────

    /**
     * @brief Load (or reload) the entire media library into the priority queue.
     *
     * Priority formula: playCounts[id] * 10 + 1
     * This ensures tracks listened to more often surface first in auto-play,
     * while every track gets at least priority 1 (never starved).
     *
     * @param allIds      All media IDs in the library.
     * @param playCounts  Map of mediaId → historical play count.
     */
    void loadLibrary(const std::vector<std::string>& allIds,
                     const std::map<std::string, int>& playCounts);

    // ── Current track ───────────────────────────────────────────────────────

    /**
     * @brief Manually set the current track (e.g. user clicks a song).
     *
     * Pushes the ID onto the history stack.
     *
     * @param id  Media ID of the track that is now playing.
     */
    void setCurrentTrack(const std::string& id);

    /** @return The ID of the track that is currently playing. */
    std::string getCurrentId() const;

    // ── Playback navigation ─────────────────────────────────────────────────

    /**
     * @brief Advance to the next track.
     *
     * - If repeat is ON  : returns currentId_ (replay same track).
     * - If shuffle is ON : picks a random track from allIds_ using rng_.
     * - Otherwise        : pops the highest-priority track from the heap.
     *                      If the heap is empty, the library is reloaded first.
     *
     * The result is pushed onto the history stack and becomes the new current.
     *
     * @param shuffle  true to override the stored shuffle flag for this call.
     * @return The media ID of the next track to play.
     */
    std::string nextTrack(bool shuffle);

    /**
     * @brief Step back to the previous track in play history.
     *
     * Moves the history cursor backward one position.
     *
     * @return The media ID of the previous track, or "" if at the oldest entry.
     */
    std::string prevTrack();

    // ── Lifecycle callbacks ─────────────────────────────────────────────────

    /**
     * @brief Called when a track finishes playing naturally.
     *
     * Increments the play count for @p id and re-inserts it into the
     * priority queue with the updated priority so future auto-play correctly
     * weights it.
     *
     * @param id  Media ID of the track that just finished.
     */
    void onTrackFinished(const std::string& id);

    // ── Queue inspection ────────────────────────────────────────────────────

    /**
     * @brief Return the next @p n tracks that *would* play (without consuming them).
     *
     * This is a read-only preview; it does not modify the heap.
     *
     * @param n  How many upcoming IDs to return.
     * @return   Vector of up to @p n upcoming media IDs, highest-priority first.
     */
    std::vector<std::string> getUpNext(int n) const;

    /**
     * @brief Force a specific track to play next by inserting it at INT_MAX priority.
     *
     * @param id  Media ID to jump-queue.
     */
    void enqueueNext(const std::string& id);

    // ── Shuffle / Repeat flags ──────────────────────────────────────────────

    void setShuffle(bool on);    ///< Enable or disable shuffle mode.
    bool isShuffleOn() const;    ///< Query current shuffle state.

    void setRepeat(bool on);     ///< Enable or disable repeat-one mode.
    bool isRepeatOn() const;     ///< Query current repeat state.

    // ── History ─────────────────────────────────────────────────────────────

    /**
     * @brief Return the full play history (newest first).
     * @return Vector of media IDs in reverse-chronological order.
     */
    std::vector<std::string> getHistory() const;

private:
    SmartQueue();  ///< Private constructor — use instance()

    HistoryStack                 history_;     ///< doubly-linked play-history list
    PriorityQueue                pq_;          ///< max-heap for upcoming tracks
    std::string                  currentId_;   ///< ID of the track currently playing
    std::vector<std::string>     allIds_;      ///< full library ID list (for reload/shuffle)
    std::map<std::string, int>   playCounts_;  ///< per-track play count
    bool                         shuffle_;     ///< shuffle mode flag
    bool                         repeat_;      ///< repeat-one mode flag
    std::mt19937                 rng_;         ///< Mersenne-Twister RNG for shuffle
};
