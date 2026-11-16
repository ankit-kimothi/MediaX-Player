#pragma once
/*
 * SearchEngine — Multi-strategy search over the media library.
 * DSA Concepts: Trie (prefix search O(m)), KMP (substring search O(n+m)),
 *               std::sort with custom comparators (O(n log n))
 * Author: Yatharth Pant
 */

#include "MediaItem.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace mediax {

// ── Trie Node ────────────────────────────────────────────────────────────────
/**
 * @brief A single node in the Trie data structure.
 *
 * Each node stores a map of its children (keyed by character) and
 * the list of media IDs whose searchable text ends at (or passes through)
 * this node.
 */
struct TrieNode {
    std::unordered_map<char, TrieNode*> children; ///< char → child node
    std::vector<std::string> mediaIds;             ///< IDs whose text ends here
    bool isEnd = false;                            ///< true if this node is a word end

    /// Recursively deletes all child nodes to prevent memory leaks.
    ~TrieNode();
};

// ── Trie ─────────────────────────────────────────────────────────────────────
/**
 * @brief A Trie (prefix tree) for efficient O(m) prefix look-ups.
 *
 * Keys inserted are lowercased searchable strings (title + artist + format …).
 * Each terminal node records which media IDs end there, enabling fast retrieval.
 */
class Trie {
public:
    Trie();
    ~Trie();

    /**
     * @brief Insert a (text, mediaId) pair.
     *        Complexity: O(m) where m = text length.
     */
    void insert(const std::string& text, const std::string& mediaId);

    /**
     * @brief Collect all media IDs whose key starts with @p prefix.
     *        Complexity: O(m + k) where m = prefix length, k = number of results.
     */
    std::vector<std::string> prefixSearch(const std::string& prefix) const;

    /// Clears the entire Trie and resets it to an empty root.
    void clear();

private:
    TrieNode* root_; ///< root node (never null after construction)

    /**
     * @brief DFS collector: traverses subtree rooted at @p node and appends
     *        all media IDs found into @p results.
     */
    void collectIds(TrieNode* node, std::vector<std::string>& results) const;
};

// ── Sort enums ────────────────────────────────────────────────────────────────

/// Fields by which the library can be sorted.
enum class SortKey {
    TITLE,       ///< Alphabetical by title
    ARTIST,      ///< Alphabetical by artist
    DURATION,    ///< Shortest to longest (or reversed)
    FILE_SIZE,   ///< Smallest to largest file (or reversed)
    PLAY_COUNT,  ///< Least to most played (or reversed)
    DATE_ADDED   ///< Oldest to newest (or reversed)
};

/// Sorting direction.
enum class SortOrder {
    ASC,  ///< Ascending (default)
    DESC  ///< Descending
};

// ── KMP free function ─────────────────────────────────────────────────────────
/**
 * @brief Knuth-Morris-Pratt substring search.
 *
 * Returns all zero-based start positions where @p pattern occurs in @p text.
 * Complexity: O(n + m) where n = text length, m = pattern length.
 *
 * @param text    The haystack string.
 * @param pattern The needle to find.
 * @return Vector of starting indices (may be empty).
 */
std::vector<int> kmpSearch(const std::string& text, const std::string& pattern);

// ── Sort free function ────────────────────────────────────────────────────────
/**
 * @brief Returns a sorted copy of @p items.
 *
 * Uses std::sort (introsort, O(n log n) average) with a lambda comparator
 * selected by @p key and @p order.
 *
 * @param items  Library snapshot to sort (passed by value deliberately).
 * @param key    Which field to compare.
 * @param order  ASC or DESC.
 * @return Sorted copy of the library.
 */
std::vector<MediaItem> sortLibrary(std::vector<MediaItem> items,
                                   SortKey key,
                                   SortOrder order);

// ── SearchEngine ──────────────────────────────────────────────────────────────
/**
 * @brief Thread-safe singleton that provides multi-strategy search over the
 *        media library.
 *
 * Usage:
 *   SearchEngine::instance().buildIndex(library);
 *   auto ids = SearchEngine::instance().search("coldplay");
 */
class SearchEngine {
public:
    /// Returns the singleton instance.
    static SearchEngine& instance();

    /**
     * @brief Rebuilds both the Trie index and the flat text index from the
     *        supplied library snapshot.
     * @param library Current media library.
     */
    void buildIndex(const std::vector<MediaItem>& library);

    /**
     * @brief Prefix search using the Trie.
     *        Complexity: O(m) where m = prefix length.
     * @param prefix Query prefix (case-insensitive).
     * @return Deduplicated list of media IDs.
     */
    std::vector<std::string> prefixSearch(const std::string& prefix) const;

    /**
     * @brief Substring search using KMP over the flat index.
     *        Complexity: O(n+m) per item, O(N*(n+m)) total.
     * @param needle Query substring (case-insensitive).
     * @return List of media IDs containing the needle.
     */
    std::vector<std::string> substringSearch(const std::string& needle) const;

    /**
     * @brief Combined search: tries prefix first (fast Trie), falls back to
     *        KMP substring search, deduplicates results.
     * @param query Arbitrary query string (case-insensitive).
     * @return Ordered, deduplicated list of matching media IDs.
     */
    std::vector<std::string> search(const std::string& query) const;

private:
    SearchEngine() = default; ///< Singleton — no public construction.

    Trie trie_; ///< Prefix index

    /// Flat list of (lowercased_searchable_text, mediaId) pairs for KMP.
    std::vector<std::pair<std::string, std::string>> flatIndex_;
};

} // namespace mediax
