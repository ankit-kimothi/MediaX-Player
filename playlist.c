#include "playlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Node* createNode(int id, const char* title, const char* genre, int energy, int tempo) {
    struct Node* newNode = malloc(sizeof(struct Node));

    if (newNode == NULL) {
        return NULL;
    }

    newNode->data.id = id;
    strcpy(newNode->data.title, title);
    strcpy(newNode->data.genre, genre);
    newNode->data.energy = energy;
    newNode->data.tempo = tempo;
    newNode->next = NULL;
    newNode->prev = NULL;

    return newNode;
}

void insertPlaylist(struct Node** head, int id, const char* title, const char* genre, int energy, int tempo) {
    struct Node* newNode = createNode(id, title, genre, energy, tempo);

    if (newNode == NULL) {
        return;
    }

    if (*head == NULL) {
        *head = newNode;
        return;
    }

    struct Node* temp = *head;

    while (temp->next != NULL) {
        temp = temp->next;
    }

    temp->next = newNode;
    newNode->prev = temp;
}

void freePlaylist(struct Node* head) {
    struct Node* curr = head;

    while (curr != NULL) {
        struct Node* next = curr->next;
        free(curr);
        curr = next;
    }
}

void pushHistory(struct StackNode** top, struct Song s) {
    struct StackNode* newNode = malloc(sizeof(struct StackNode));

    if (newNode == NULL) {
        return;
    }

    newNode->data = s;
    newNode->next = *top;
    *top = newNode;
}

struct Song popHistory(struct StackNode** top) {
    if (*top == NULL) {
        struct Song emptySong = {0};
        emptySong.id = -1;
        strcpy(emptySong.title, "None");
        strcpy(emptySong.genre, "None");
        return emptySong;
    }

    struct StackNode* temp = *top;
    struct Song popped = temp->data;

    *top = temp->next;
    free(temp);

    return popped;
}

void displayHistory(struct StackNode* top) {
    printf("\nPlayback History:\n");

    if (top == NULL) {
        printf("No songs in history yet.\n");
        return;
    }

    struct StackNode* temp = top;
    int index = 1;

    while (temp != NULL) {
        printf("%d. %s (%s) [ID: %d]\n",
               index, temp->data.title,
               temp->data.genre, temp->data.id);

        temp = temp->next;
        index++;
    }
}

void freeHistory(struct StackNode* top) {
    struct StackNode* curr = top;

    while (curr != NULL) {
        struct StackNode* next = curr->next;
        free(curr);
        curr = next;
    }
}
