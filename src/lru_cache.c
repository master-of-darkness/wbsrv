#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include "lru_cache.h"

LRUCache *createCache(int size) {
    LRUCache *newCache = (LRUCache *) malloc(sizeof(LRUCache));
    Table *table = createTable(size * 2);
    List *list = createList(size);
    newCache->table = table;
    newCache->list = list;
    return newCache;
}

Node *createNode(char *key, char *value) {
    Node *newNode = (Node *) malloc(sizeof(Node));
    newNode->key = key;
    newNode->value = value;
    newNode->next = NULL;
    newNode->prev = NULL;
    newNode->hashNext = NULL;
    return newNode;
}

List *createList(int capacity) {
    List *newList = (List *) malloc(sizeof(List));
    newList->size = 0;
    newList->capacity = capacity;
    newList->head = newList->tail = NULL;
    return newList;
}

Table *createTable(int capacity) {
    Table *newhash = (Table *) malloc(sizeof(Table));
    newhash->capacity = capacity;
    newhash->array = (Node **) malloc(sizeof(Node) * capacity);
    for (size_t i = 0; i < capacity; i++)
        newhash->array[i] = NULL;
    return newhash;
}

static size_t getHashCode(Table *table, const char *source) {
    if (source == NULL)
        return 0;
    size_t hash = 0;
    while (*source != '\0') {
        char c = *source++;
        int a = c - '0';
        hash = (hash * 10) + a;
    }
    return hash % table->capacity;
}

void evictCache(LRUCache *cache) {
    Table *table = cache->table;
    List *list = cache->list;

    Node *entry = list->head;
    // return if empty
    if (list->head == NULL)
        return;
    // remove head and tail if only one node
    if (list->head == list->tail) {
        list->head = NULL;
        list->tail = NULL;
    } else {
        // remove entry from list with multiple nodes
        list->head = entry->next;
        list->size = list->size - 1;
        list->head->prev = NULL;
    }

    // remove from map
    size_t hashCode = getHashCode(table, entry->key);
    Node **indirect = &table->array[hashCode];
    while ((*indirect) != entry)
        indirect = &(*indirect)->next;
    *indirect = entry->next;

    free(entry);
}

void moveToFront(LRUCache *cache, char *key) {
    Table *table = cache->table;
    List *list = cache->list;

    // return if only element in list
    if (list->size == 1)
        return;

    size_t hashCode = getHashCode(table, key);
    Node *curr = table->array[hashCode];

    // find in hashMap
    while (curr) {
        if (strcmp(curr->key, key) == 0)
            break;
        curr = curr->hashNext;
    }
    // return if doesn't exist
    if (curr == NULL)
        return;

    // move curr to latest/tail of list
    if (curr->prev == NULL) {
        // if head in list
        curr->prev = list->tail;
        list->head = curr->next;
        list->head->prev = NULL;
        list->tail->next = curr;
        list->tail = curr;
        list->tail->next = NULL;
        return;
    }

    if (curr->next == NULL) // already latest at tail
        return;

    // if curr in middle
    curr->next->prev = curr->prev;
    curr->prev->next = curr->next;
    curr->next = NULL;
    list->tail->next = curr;
    curr->prev = list->tail;
    list->tail = curr;
}

void addToList(LRUCache *cache, Node *valNode) {
    List *list = cache->list;
    if (list->size == list->capacity)
        evictCache(cache);

    if (list->head == NULL) {
        list->head = list->tail = valNode;
        list->size = 1;
        return;
    }

    valNode->prev = list->tail;
    list->tail->next = valNode;
    list->tail = valNode;
    list->size = list->size + 1;
    return;
}

int addToHash(Table *table, Node *valNode) {
    size_t hashCode = getHashCode(table, valNode->key);
    if (table->array[hashCode] != NULL) {
        // check if is in hash

        Node *curr = table->array[hashCode];

        while (curr->hashNext != NULL) {
            if (strcmp(curr->key, valNode->key) == 0) {
                curr->value = valNode->value;
                return 1;
            }
            curr = curr->hashNext;
        }
        // last node
        if (strcmp(curr->key, valNode->key) == 0) {
            curr->value = valNode->value;
            return 1;
        }

        // add after last node
        curr->hashNext = valNode;
        return 0;
    } else {
        table->array[hashCode] = valNode;
        return 0;
    }
}

void put(LRUCache *cache, char *key, char *value) {
    Node *valNode = createNode(key, value);

    if (addToHash(cache->table, valNode)) // already in list
        moveToFront(cache, valNode->key);
    else
        addToList(cache, valNode);
}

char *get(LRUCache *cache, char *key) {
    Table *table = cache->table;
    size_t hashCode = getHashCode(table, key);
    Node *curr = table->array[hashCode];
    while (curr) {
        if (strcmp(curr->key, key) == 0) {
            moveToFront(cache, key);
            return curr->value;
        }
        curr = curr->hashNext;
    }
    return NULL;
}

void printCache(LRUCache *cache) {
    Node *temp = cache->list->head;
    for (size_t i = 0; i < cache->list->size; i++) {
        printf("%s %s \n", temp->key, temp->value);
        temp = temp->next;
    }
}
