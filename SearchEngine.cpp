/**
 * @file SearchEngine.cpp
 * @brief Search engine implementation using Trie and KMP string search algorithms.
 *
 * DSA Concepts:
 *  - Trie (Prefix Tree): O(m) search where m is the prefix length. Allows ultra-fast
 *    auto-complete style search across track titles and artists.
 *  - Knuth-Morris-Pratt (KMP) Algorithm: O(n + m) pattern matching using Longest
 *    Prefix Suffix (LPS) array failure function to avoid backtracking.
 *  - Custom Multi-key Sorting: std::sort with lambda comparators for Title, Artist,
 *    Duration, File Size, and Play Count.
 *
 * Author: Yatharth Pant
 */

#include "SearchEngine.h"
#include <algorithm>
#include <unordered_set>

namespace mediax {

// Helper: lowercase a string
static std::string toLowerString(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// §1  TrieNode and Trie Implementation
// ─────────────────────────────────────────────────────────────────────────────

TrieNode::~TrieNode() {
    // Recursively clean up children in Trie destructor
    for (auto& pair : children) {
        delete pair.second;
    }
    children.clear();
}

Trie::Trie() {
    root_ = new TrieNode();
}

Trie::~Trie() {
    delete root_;
    root_ = nullptr;
}

void Trie::clear() {
    delete root_;
    root_ = new TrieNode();
}

void Trie::insert(const std::string& text, const std::string& mediaId) {
    if (text.empty() || mediaId.empty()) return;

    TrieNode* curr = root_;
    for (char ch : text) {
        if (curr->children.find(ch) == curr->children.end()) {
            curr->children[ch] = new TrieNode();
        }
        curr = curr->children[ch];
        // Store media ID along path for prefix matching
        curr->mediaIds.push_back(mediaId);
    }
    curr->isEnd = true;
}

void Trie::collectIds(TrieNode* node, std::vector<std::string>& results) const {
    if (!node) return;
    for (const auto& id : node->mediaIds) {
        results.push_back(id);
    }
}

std::vector<std::string> Trie::prefixSearch(const std::string& prefix) const {
    std::vector<std::string> results;
    if (prefix.empty() || !root_) return results;

    TrieNode* curr = root_;
    for (char ch : prefix) {
        auto it = curr->children.find(ch);
        if (it == curr->children.end()) {
            return results; // Prefix not found
        }
        curr = it->second;
    }

    // Collect IDs attached to this prefix node
    collectIds(curr, results);

    // Deduplicate while preserving order
    std::unordered_set<std::string> seen;
    std::vector<std::string> deduped;
    for (const auto& id : results) {
        if (seen.insert(id).second) {
            deduped.push_back(id);
        }
    }
    return deduped;
}

// ─────────────────────────────────────────────────────────────────────────────
// §2  KMP Substring Search Implementation
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Builds the Longest Prefix Suffix (LPS) table for KMP.
 * lps[i] stores the length of the longest proper prefix of pattern[0..i]
 * that is also a suffix of pattern[0..i].
 */
static std::vector<int> buildLPS(const std::string& pattern) {
    int m = static_cast<int>(pattern.length());
    std::vector<int> lps(m, 0);
    int len = 0; // length of previous longest prefix suffix
    int i = 1;

    while (i < m) {
        if (pattern[i] == pattern[len]) {
            len++;
            lps[i] = len;
            i++;
        } else {
            if (len != 0) {
                len = lps[len - 1]; // fallback without incrementing i
            } else {
                lps[i] = 0;
                i++;
            }
        }
    }
    return lps;
}

std::vector<int> kmpSearch(const std::string& text, const std::string& pattern) {
    std::vector<int> matches;
    int n = static_cast<int>(text.length());
    int m = static_cast<int>(pattern.length());
    if (m == 0 || n == 0 || m > n) return matches;

    // Precompute failure function: O(m)
    std::vector<int> lps = buildLPS(pattern);

    int i = 0; // index for text
    int j = 0; // index for pattern

    // Scan text: O(n)
    while (i < n) {
        if (pattern[j] == text[i]) {
            i++;
            j++;
        }

        if (j == m) {
            // Match found at index (i - j)
            matches.push_back(i - j);
            j = lps[j - 1]; // continue searching for next occurrence
        } else if (i < n && pattern[j] != text[i]) {
            // Mismatch after j matches
            if (j != 0) {
                j = lps[j - 1]; // use LPS table to skip redundant checks
            } else {
                i++;
            }
        }
    }
    return matches;
}

// ─────────────────────────────────────────────────────────────────────────────
// §3  Library Multi-key Sorting (std::sort + Comparators)
// ─────────────────────────────────────────────────────────────────────────────

std::vector<MediaItem> sortLibrary(std::vector<MediaItem> items,
                                   SortKey key,
                                   SortOrder order) {
    auto comp = [key, order](const MediaItem& a, const MediaItem& b) -> bool {
        bool result = false;
        switch (key) {
            case SortKey::TITLE: {
                std::string titleA = toLowerString(a.title);
                std::string titleB = toLowerString(b.title);
                result = (titleA < titleB);
                break;
            }
            case SortKey::ARTIST: {
                std::string artistA = toLowerString(a.artist);
                std::string artistB = toLowerString(b.artist);
                result = (artistA < artistB);
                break;
            }
            case SortKey::DURATION:
                result = (a.durationSecs < b.durationSecs);
                break;
            case SortKey::FILE_SIZE:
                result = (a.fileSizeBytes < b.fileSizeBytes);
                break;
            case SortKey::PLAY_COUNT:
                result = (a.playCount < b.playCount);
                break;
            case SortKey::DATE_ADDED:
                result = (a.addedTimestamp < b.addedTimestamp);
                break;
        }

        return (order == SortOrder::ASC) ? result : !result;
    };

    // std::sort uses Introsort (hybrid of QuickSort, HeapSort, and InsertionSort): O(n log n)
    std::sort(items.begin(), items.end(), comp);
    return items;
}

// ─────────────────────────────────────────────────────────────────────────────
// §4  SearchEngine Singleton Implementation
// ─────────────────────────────────────────────────────────────────────────────

SearchEngine& SearchEngine::instance() {
    static SearchEngine s_instance;
    return s_instance;
}

void SearchEngine::buildIndex(const std::vector<MediaItem>& library) {
    trie_.clear();
    flatIndex_.clear();

    for (const auto& item : library) {
        // Build lowercased searchable representation: "title artist format path"
        std::string searchable = toLowerString(item.title + " " + item.artist + " " +
                                              item.format + " " + item.filePath);

        // 1. Insert words into Trie for fast prefix search
        std::string word;
        for (char ch : searchable) {
            if (ch == ' ' || ch == '/' || ch == '\\' || ch == '.' || ch == '-') {
                if (!word.empty()) {
                    trie_.insert(word, item.id);
                    word.clear();
                }
            } else {
                word += ch;
            }
        }
        if (!word.empty()) {
            trie_.insert(word, item.id);
        }

        // 2. Also index full string for complete match
        trie_.insert(searchable, item.id);

        // 3. Store in flatIndex_ for KMP substring search
        flatIndex_.push_back({searchable, item.id});
    }
}

std::vector<std::string> SearchEngine::prefixSearch(const std::string& prefix) const {
    std::string lowerPrefix = toLowerString(prefix);
    return trie_.prefixSearch(lowerPrefix);
}

std::vector<std::string> SearchEngine::substringSearch(const std::string& needle) const {
    std::string lowerNeedle = toLowerString(needle);
    std::vector<std::string> results;
    std::unordered_set<std::string> seen;

    for (const auto& entry : flatIndex_) {
        // Use Knuth-Morris-Pratt for exact substring matching: O(n + m)
        auto matches = kmpSearch(entry.first, lowerNeedle);
        if (!matches.empty()) {
            if (seen.insert(entry.second).second) {
                results.push_back(entry.second);
            }
        }
    }
    return results;
}

std::vector<std::string> SearchEngine::search(const std::string& query) const {
    if (query.empty()) return {};

    // First try fast prefix search via Trie: O(m)
    std::vector<std::string> results = prefixSearch(query);

    // Fall back or augment with KMP substring search
    std::vector<std::string> subResults = substringSearch(query);

    // Combine and deduplicate
    std::unordered_set<std::string> seen(results.begin(), results.end());
    for (const auto& id : subResults) {
        if (seen.insert(id).second) {
            results.push_back(id);
        }
    }

    return results;
}

} // namespace mediax

