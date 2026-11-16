/**
 * @file SmartQueue.cpp
 * @brief Implementation of the Smart Queue module for MediaX.
 *
 * DSA Concepts Implemented:
 *  - Doubly-Linked List (HistoryStack): O(1) head insertion and removal,
 *    cursor-based bi-directional traversal for forward/backward history navigation.
 *  - Max-Heap Priority Queue (PriorityQueue): Custom vector-based binary heap with
 *    heapifyUp / heapifyDown maintaining max-heap property in O(log n).
 *
 * Author: Ankit Kimothi
 */

#include "SmartQueue.h"
#include <chrono>

// ─────────────────────────────────────────────────────────────────────────────
// §1  HistoryStack Implementation (Doubly-Linked List)
// ─────────────────────────────────────────────────────────────────────────────

HistoryStack::HistoryStack()
    : head_(nullptr), tail_(nullptr), cursor_(nullptr), size_(0) {}

HistoryStack::~HistoryStack() {
    // Delete all nodes from head to tail to prevent memory leaks
    Node* curr = head_;
    while (curr != nullptr) {
        Node* nextNode = curr->next;
        delete curr;
        curr = nextNode;
    }
    head_ = nullptr;
    tail_ = nullptr;
    cursor_ = nullptr;
    size_ = 0;
}

void HistoryStack::pushFront(const std::string& id) {
    if (id.empty()) return;

    // Create new doubly-linked list node
    Node* newNode = new Node(id);

    // O(1) insertion at head
    if (head_ == nullptr) {
        head_ = newNode;
        tail_ = newNode;
    } else {
        newNode->next = head_;
        head_->prev = newNode;
        head_ = newNode;
    }

    size_++;
    cursor_ = head_; // Reset cursor to newest entry

    // Enforce bounded history: if size exceeds MAX_SIZE, drop the oldest node (tail)
    if (size_ > MAX_SIZE && tail_ != nullptr) {
        Node* oldTail = tail_;
        tail_ = tail_->prev;
        if (tail_ != nullptr) {
            tail_->next = nullptr;
        } else {
            head_ = nullptr;
        }
        delete oldTail;
        size_--;
    }
}

std::string HistoryStack::popFront() {
    if (head_ == nullptr) return "";

    Node* oldHead = head_;
    std::string id = oldHead->mediaId;

    // O(1) removal at head
    head_ = head_->next;
    if (head_ != nullptr) {
        head_->prev = nullptr;
    } else {
        tail_ = nullptr;
    }

    delete oldHead;
    size_--;
    cursor_ = head_;
    return id;
}

bool HistoryStack::goBack() {
    // If cursor is not set, begin at head
    if (cursor_ == nullptr) {
        cursor_ = head_;
        return cursor_ != nullptr;
    }
    // Older entries are along the 'next' pointer chain (since pushFront puts newer at head)
    if (cursor_->next != nullptr) {
        cursor_ = cursor_->next;
        return true;
    }
    return false; // Reached oldest entry
}

bool HistoryStack::goForward() {
    if (cursor_ == nullptr) {
        cursor_ = head_;
        return cursor_ != nullptr;
    }
    // Newer entries are along the 'prev' pointer chain
    if (cursor_->prev != nullptr) {
        cursor_ = cursor_->prev;
        return true;
    }
    return false; // Already at newest entry
}

std::string HistoryStack::peekFront() const {
    return (head_ != nullptr) ? head_->mediaId : "";
}

std::string HistoryStack::getCurrent() const {
    return (cursor_ != nullptr) ? cursor_->mediaId : "";
}

std::vector<std::string> HistoryStack::toVector() const {
    std::vector<std::string> list;
    list.reserve(size_);
    Node* curr = head_;
    while (curr != nullptr) {
        list.push_back(curr->mediaId);
        curr = curr->next;
    }
    return list;
}

size_t HistoryStack::size() const {
    return size_;
}

// ─────────────────────────────────────────────────────────────────────────────
// §2  PriorityQueue Implementation (Binary Max-Heap over std::vector)
// ─────────────────────────────────────────────────────────────────────────────

void PriorityQueue::push(const std::string& id, int priority) {
    PQEntry entry;
    entry.mediaId = id;
    entry.priority = priority;

    // Append at the end of the heap array: O(1)
    heap_.push_back(entry);

    // Bubble up to restore the max-heap invariant: O(log n)
    heapifyUp(static_cast<int>(heap_.size()) - 1);
}

std::string PriorityQueue::pop() {
    if (heap_.empty()) return "";

    std::string topId = heap_[0].mediaId;

    // Swap root with last element and shrink: O(1)
    heap_[0] = heap_.back();
    heap_.pop_back();

    // Sift down the new root: O(log n)
    if (!heap_.empty()) {
        heapifyDown(0);
    }

    return topId;
}

bool PriorityQueue::empty() const {
    return heap_.empty();
}

size_t PriorityQueue::size() const {
    return heap_.size();
}

void PriorityQueue::heapifyUp(int i) {
    // Max-Heap invariant: parent node priority must be >= child node priority
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (heap_[parent].priority < heap_[i].priority) {
            std::swap(heap_[parent], heap_[i]);
            i = parent;
        } else {
            break;
        }
    }
}

void PriorityQueue::heapifyDown(int i) {
    int n = static_cast<int>(heap_.size());
    while (true) {
        int largest = i;
        int left = 2 * i + 1;
        int right = 2 * i + 2;

        if (left < n && heap_[left].priority > heap_[largest].priority) {
            largest = left;
        }
        if (right < n && heap_[right].priority > heap_[largest].priority) {
            largest = right;
        }

        if (largest != i) {
            std::swap(heap_[i], heap_[largest]);
            i = largest;
        } else {
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// §3  SmartQueue Singleton Implementation
// ─────────────────────────────────────────────────────────────────────────────

SmartQueue& SmartQueue::instance() {
    static SmartQueue s_instance;
    return s_instance;
}

SmartQueue::SmartQueue()
    : currentId_(""), shuffle_(false), repeat_(false) {
    // Seed standard Mersenne Twister RNG with current high-resolution clock
    unsigned seed = static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count());
    rng_.seed(seed);
}

void SmartQueue::loadLibrary(const std::vector<std::string>& allIds,
                             const std::map<std::string, int>& playCounts) {
    allIds_ = allIds;
    playCounts_ = playCounts;

    // Clear existing priority queue and re-populate with weighted priority
    while (!pq_.empty()) {
        pq_.pop();
    }

    for (const auto& id : allIds_) {
        int count = 0;
        auto it = playCounts_.find(id);
        if (it != playCounts_.end()) {
            count = it->second;
        }
        // Formula: priority = count * 10 + 1 (base priority 1 prevents starvation)
        int prio = count * 10 + 1;
        pq_.push(id, prio);
    }
}

void SmartQueue::setCurrentTrack(const std::string& id) {
    currentId_ = id;
    if (!id.empty()) {
        history_.pushFront(id);
    }
}

std::string SmartQueue::getCurrentId() const {
    return currentId_;
}

std::string SmartQueue::nextTrack(bool shuffle) {
    if (repeat_ && !currentId_.empty()) {
        // Repeat mode active: repeat the current track
        return currentId_;
    }

    std::string nextId = "";

    if (shuffle || shuffle_) {
        if (!allIds_.empty()) {
            std::uniform_int_distribution<size_t> dist(0, allIds_.size() - 1);
            nextId = allIds_[dist(rng_)];
        }
    } else {
        // Dequeue from Max-Heap Priority Queue
        if (pq_.empty()) {
            // If priority queue exhausted, reload from known library
            loadLibrary(allIds_, playCounts_);
        }
        if (!pq_.empty()) {
            nextId = pq_.pop();
        }
    }

    if (!nextId.empty()) {
        currentId_ = nextId;
        history_.pushFront(nextId);
    }
    return currentId_;
}

std::string SmartQueue::prevTrack() {
    if (history_.goBack()) {
        std::string prevId = history_.getCurrent();
        if (!prevId.empty()) {
            currentId_ = prevId;
            return currentId_;
        }
    }
    return currentId_;
}

void SmartQueue::onTrackFinished(const std::string& id) {
    if (id.empty()) return;
    playCounts_[id]++;

    // Re-insert into priority queue with boosted priority
    int newPrio = playCounts_[id] * 10 + 1;
    pq_.push(id, newPrio);
}

std::vector<std::string> SmartQueue::getUpNext(int n) const {
    std::vector<std::string> result;
    if (n <= 0) return result;

    // Make a temporary copy of the priority queue to peek without modifying
    PriorityQueue copy = pq_;
    for (int i = 0; i < n && !copy.empty(); ++i) {
        result.push_back(copy.pop());
    }
    return result;
}

void SmartQueue::enqueueNext(const std::string& id) {
    if (id.empty()) return;
    // Force to top of heap by using maximum integer priority
    pq_.push(id, INT_MAX);
}

void SmartQueue::setShuffle(bool on) {
    shuffle_ = on;
}

bool SmartQueue::isShuffleOn() const {
    return shuffle_;
}

void SmartQueue::setRepeat(bool on) {
    repeat_ = on;
}

bool SmartQueue::isRepeatOn() const {
    return repeat_;
}

std::vector<std::string> SmartQueue::getHistory() const {
    return history_.toVector();
}

