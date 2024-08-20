#pragma once

typedef struct Node {
    char *key;
    char *value;
    struct Node *next;
    struct Node *prev;
    struct Node *hashNext;
} Node;

typedef struct Table {
    int capacity;
    Node * *array;
} Table;

typedef struct List {
    int size;
    int capacity;
    Node *head;
    Node *tail;
} List;

typedef struct LRUCache {
    Table *table;
    List *list;
} LRUCache;

static size_t getHashCode(Table *table, const char *source);

LRUCache *createCache(int size);

Node *createNode(char *key, char *value);

List *createList(int capacity);

Table *createTable(int capacity);

void evictCache(LRUCache *cache);

void moveToFront(LRUCache *cache, char *key);

void addToList(LRUCache *cache, Node *valNode);

int addToHash(Table *table, Node *valNode);

void put(LRUCache *cache, char *key, char *value);

char *get(LRUCache *cache, char *key);

void printCache(LRUCache *cache);
