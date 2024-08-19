#pragma once

typedef struct st_cache_node_t {
    int key;
    void *value;
    struct st_cache_node_t *prev;
    struct st_cache_node_t *next;
} cache_node_t;

typedef struct st_lru_cache_t {
    int capacity;
    int size;
    cache_node_t *head;
    cache_node_t *tail;
    cache_node_t **hashTable;
} lru_cache_t;

typedef struct st_doc_cache_row_t {
    const char* content_type;
    const char* content_text;
} cache_row_t;
