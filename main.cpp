
#include <iostream>
#include <cstring>
#include <cstdlib>
#include "playlist.h"

using namespace std;

struct QueueSong {
    struct Song song;
    int priority;
    int isManual;
};

struct QueueSong heap[50];
int totalSongs = 0;

void swapSongs(int a, int b) {
    struct QueueSong temp = heap[a];
    heap[a] = heap[b];
    heap[b] = temp;
}

void heapifyUp(int i) {
    while (i > 0) {
        int parent = (i - 1) / 2;

        if (heap[i].priority > heap[parent].priority) {
            swapSongs(i, parent);
            i = parent;
        } else {
            break;
        }
    }
}

void heapifyDown(int i) {
    while (true) {
        int largest = i;
        int left = 2 * i + 1;
        int right = 2 * i + 2;

        if (left < totalSongs &&
            heap[left].priority > heap[largest].priority) {
            largest = left;
        }

        if (right < totalSongs &&
            heap[right].priority > heap[largest].priority) {
            largest = right;
        }

        if (largest == i) {
            break;
        }

        swapSongs(i, largest);
        i = largest;
    }
}

int getManhattanDistance(struct Song a, struct Song b) {
    return abs(a.energy - b.energy) +
           abs(a.tempo - b.tempo);
}

void addToQueue(int id, const char* title, const char* genre,
                int energy, int tempo, int manual) {
    if (totalSongs >= 50) {
        cout << "Queue limit reached.\n";
        return;
    }

    heap[totalSongs].song.id = id;
    strcpy(heap[totalSongs].song.title, title);
    strcpy(heap[totalSongs].song.genre, genre);
    heap[totalSongs].song.energy = energy;
    heap[totalSongs].song.tempo = tempo;
    heap[totalSongs].isManual = manual;

    if (manual == 1) {
        heap[totalSongs].priority = 100;
    } else {
        heap[totalSongs].priority = 1;
    }

    totalSongs++;
    heapifyUp(totalSongs - 1);
}

struct QueueSong getNextFromQueue() {
    struct QueueSong emptySong = {0};

    emptySong.song.id = -1;
    strcpy(emptySong.song.title, "None");
    strcpy(emptySong.song.genre, "None");

    if (totalSongs == 0) {
        return emptySong;
    }

    struct QueueSong nextSong = heap[0];

    heap[0] = heap[totalSongs - 1];
    totalSongs--;

    if (totalSongs > 0) {
        heapifyDown(0);
    }

    return nextSong;
}

void recalculateQueue(struct Song current) {
    for (int i = 0; i < totalSongs; i++) {
        if (heap[i].isManual == 1) {
            continue;
        }

        int distance = getManhattanDistance(current, heap[i].song);
        heap[i].priority = 50 - distance;

        if (strcmp(heap[i].song.genre, current.genre) == 0) {
            heap[i].priority += 10;
        }
    }

    for (int i = totalSongs / 2 - 1; i >= 0; i--) {
        heapifyDown(i);
    }
}

void printQueue() {
    cout << "\nSmart Queue State:\n";

    if (totalSongs == 0) {
        cout << "No tracks waiting in queue.\n";
        return;
    }

    for (int i = 0; i < totalSongs; i++) {
        cout << i + 1 << ". " << heap[i].song.title
             << " (" << heap[i].song.genre << ")"
             << " [Score: " << heap[i].priority << "]";

        if (heap[i].isManual == 1) {
            cout << " [User Queued]";
        }

        cout << "\n";
    }
}

int main() {
    struct Node* playlist = NULL;
    struct StackNode* history = NULL;

    insertPlaylist(&playlist, 101, "Bohemian Rhapsody", "Rock", 8, 4);
    insertPlaylist(&playlist, 102, "Shape of You", "Pop", 7, 7);
    insertPlaylist(&playlist, 103, "Hotel California", "Rock", 5, 5);
    insertPlaylist(&playlist, 104, "Blinding Lights", "Pop", 9, 8);
    insertPlaylist(&playlist, 105, "Take Five", "Jazz", 3, 4);

    addToQueue(201, "Comfortably Numb", "Rock", 6, 4, 0);
    addToQueue(202, "Levitating", "Pop", 8, 8, 0);
    addToQueue(203, "Sweet Child O Mine", "Rock", 9, 7, 0);
    addToQueue(204, "Bad Romance", "Pop", 9, 9, 0);
    addToQueue(205, "So What", "Jazz", 3, 3, 0);

    struct Node* currentTrack = playlist;
    int choice = 0;

    while (choice != 6) {
        cout << "\nMedia-X Audio Engine\n";

        if (currentTrack != NULL) {
            cout << "Playing: " << currentTrack->data.title
                 << " | " << currentTrack->data.genre
                 << " | Energy: " << currentTrack->data.energy
                 << " | Tempo: " << currentTrack->data.tempo << "\n";
        } else {
            cout << "Playing: None\n";
        }

        cout << "\n1. Next Track\n";
        cout << "2. Previous Track\n";
        cout << "3. Play Recommendation from Queue\n";
        cout << "4. Show History\n";
        cout << "5. View Smart Queue\n";
        cout << "6. Exit\n";
        cout << "Select option: ";

        cin >> choice;

        if (choice == 1) {
            if (currentTrack != NULL && currentTrack->next != NULL) {
                pushHistory(&history, currentTrack->data);
                currentTrack = currentTrack->next;
                recalculateQueue(currentTrack->data);
            } else {
                cout << "\nReached end of the playlist.\n";
            }
        }
        else if (choice == 2) {
            if (currentTrack != NULL && currentTrack->prev != NULL) {
                pushHistory(&history, currentTrack->data);
                currentTrack = currentTrack->prev;
                recalculateQueue(currentTrack->data);
            } else {
                cout << "\nAlready at the beginning of playlist.\n";
            }
        }
        else if (choice == 3) {
            if (currentTrack != NULL) {
                pushHistory(&history, currentTrack->data);
            }

            struct QueueSong picked = getNextFromQueue();

            if (picked.song.id != -1) {
                cout << "\nSelected next: " << picked.song.title << "\n";

                insertPlaylist(&playlist, picked.song.id,
                               picked.song.title, picked.song.genre,
                               picked.song.energy, picked.song.tempo);

                struct Node* temp = playlist;

                while (temp->next != NULL) {
                    temp = temp->next;
                }

                currentTrack = temp;
                recalculateQueue(currentTrack->data);
            } else {
                cout << "\nQueue is empty.\n";
            }
        }
        else if (choice == 4) {
            displayHistory(history);
        }
        else if (choice == 5) {
            if (currentTrack != NULL) {
                recalculateQueue(currentTrack->data);
            }

            printQueue();
        }
        else if (choice == 6) {
            cout << "\nClosing player.\n";
        }
        else {
            cout << "\nInvalid input, try again.\n";
        }
    }

    freePlaylist(playlist);
    freeHistory(history);

    return 0;
}
