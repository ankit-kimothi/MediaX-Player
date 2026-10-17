#ifndef PLAYLIST_H
#define PLAYLIST_H

#ifdef __cplusplus
extern "C" {
#endif


// Adhiraj Singh
struct Song {
    int id;
    char title[50];
    char genre[30];
    int energy; // 1 (low) to 10 (high)
    int tempo;  // 1 (slow) to 10 (fast)
};

// Doubly Linked List Node for main navigation (Next / Previous track)
// Ankit Kimothi
struct Node {
    struct Song data;
    struct Node* next;
    struct Node* prev;
};

// Stack Node for Recently Played history stack (LIFO ordering)
// Yatharth Pant
struct StackNode {
    struct Song data;
    struct StackNode* next;
};


// Playlist Operations (Doubly Linked List)

// Yatharth Pant & Ankit Kimothi
struct Node* createNode(int id, const char* title, const char* genre, int energy, int tempo);
void insertPlaylist(struct Node** head, int id, const char* title, const char* genre, int energy, int tempo);
void freePlaylist(struct Node* head);

// Playback History Operations (Stack)

// Yatharth Pant
void pushHistory(struct StackNode** top, struct Song s);
struct Song popHistory(struct StackNode** top);
void displayHistory(struct StackNode* top);
void freeHistory(struct StackNode* top);

#ifdef __cplusplus
}
#endif

#endif