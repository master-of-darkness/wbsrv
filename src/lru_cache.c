#include "lru_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper function to create a new node
static cache_node_t* lru_create_node(int key, void *value, size_t value_size) {
    cache_node_t *node = (cache_node_t*)malloc(sizeof(cache_node_t));
    node->key = key;
    node->value = malloc(value_size);
    memcpy(node->value, value, value_size);
    node->prev = NULL;
    node->next = NULL;
    return node;
}

// Helper function to remove a node
static void lru_remove_node(lru_cache_t *cache, cache_node_t *node) {
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        cache->head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        cache->tail = node->prev;
    }
}

// Helper function to add a node at the head
static void lru_add_node_at_head(lru_cache_t *cache, cache_node_t *node) {
    node->next = cache->head;
    node->prev = NULL;

    if (cache->head) {
        cache->head->prev = node;
    } else {
        cache->tail = node;
    }

    cache->head = node;
}

// Create LRUCache
static lru_cache_t* lru_create_cache(int capacity) {
    lru_cache_t *cache = (lru_cache_t*)malloc(sizeof(lru_cache_t));
    cache->capacity = capacity;
    cache->size = 0;
    cache->head = NULL;
    cache->tail = NULL;
    cache->hashTable = (cache_node_t**)calloc(capacity, sizeof(cache_node_t*));
    return cache;
}

// Get value from LRUCache
static void* lru_get(lru_cache_t *cache, int key) {
    cache_node_t *node = cache->hashTable[key % cache->capacity];

    while (node) {
        if (node->key == key) {
            // Move accessed node to the head (most recently used)
            lru_remove_node(cache, node);
            lru_add_node_at_head(cache, node);
            return node->value;
        }
        node = node->next;
    }

    // Key not found
    return NULL;
}

// Put value in LRUCache
static void lru_put(lru_cache_t *cache, int key, void *value, size_t value_size) {
    cache_node_t *node = cache->hashTable[key % cache->capacity];

    while (node) {
        if (node->key == key) {
            // Update value and move node to the head
            memcpy(node->value, value, value_size);
            lru_remove_node(cache, node);
            lru_add_node_at_head(cache, node);
            return;
        }
        node = node->next;
    }

    // If cache is full, remove the least recently used item
    if (cache->size == cache->capacity) {
        cache_node_t *tail = cache->tail;
        lru_remove_node(cache, tail);

        // Remove from hash table
        cache_node_t **hashSlot = &cache->hashTable[tail->key % cache->capacity];
        while (*hashSlot && (*hashSlot)->key != tail->key) {
            hashSlot = &(*hashSlot)->next;
        }
        if (*hashSlot) {
            *hashSlot = (*hashSlot)->next;
        }

        free(tail->value);
        free(tail);
        cache->size--;
    }

    // Add new node
    cache_node_t *newNode = lru_create_node(key, value, value_size);
    lru_add_node_at_head(cache, newNode);

    // Add to hash table
    newNode->next = cache->hashTable[key % cache->capacity];
    cache->hashTable[key % cache->capacity] = newNode;
    cache->size++;
}

// Free LRUCache
static void lru_free_cache(lru_cache_t *cache) {
    cache_node_t *current = cache->head;
    while (current) {
        cache_node_t *next = current->next;
        free(current->value);
        free(current);
        current = next;
    }
    free(cache->hashTable);
    free(cache);
}
