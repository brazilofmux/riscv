#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* Linker symbols for heap boundaries */
extern char __heap_start[];
extern char __heap_end[];

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

typedef struct {
    size_t size;
} header_t;

typedef struct free_node {
    header_t header;
    struct free_node *next;
    struct free_node *prev;
} free_node_t;

typedef struct {
    size_t size;
} footer_t;

#define ALLOCATED_MASK 0x1
#define SIZE_MASK (~(size_t)0x7)

#define HEADER_SIZE ALIGN(sizeof(header_t))
#define FOOTER_SIZE (sizeof(footer_t))
#define MIN_BLOCK_SIZE ALIGN(sizeof(free_node_t) + FOOTER_SIZE)

#define NUM_BINS 8
#define MAX_SMALL_BIN_SIZE 64

static free_node_t bins[NUM_BINS];
static int heap_initialized = 0;

static size_t get_size(header_t *block) {
    return block->size & SIZE_MASK;
}

static int is_allocated(header_t *block) {
    return block->size & ALLOCATED_MASK;
}

static void set_footer(header_t *block, size_t size) {
    footer_t *footer = (footer_t *)((char *)block + size - FOOTER_SIZE);
    footer->size = size;
}

static int get_bin_index(size_t size) {
    if (size > MAX_SMALL_BIN_SIZE) return NUM_BINS - 1;
    return (size - 16) / 8;
}

static free_node_t *to_free_node(header_t *block) {
    return (free_node_t *)block;
}

static header_t *to_header(free_node_t *node) {
    return (header_t *)node;
}

static void add_to_free_list(header_t *block) {
    size_t size = get_size(block);
    int idx = get_bin_index(size);
    free_node_t *node = to_free_node(block);
    free_node_t *sentinel = &bins[idx];

    node->next = sentinel->next;
    node->prev = sentinel;
    sentinel->next->prev = node;
    sentinel->next = node;
}

static void remove_from_free_list(header_t *block) {
    free_node_t *node = to_free_node(block);

    node->prev->next = node->next;
    node->next->prev = node->prev;

    node->next = NULL;
    node->prev = NULL;
}

static void init_heap(void) {
    if (heap_initialized) return;

    for (int i = 0; i < NUM_BINS; i++) {
        bins[i].next = &bins[i];
        bins[i].prev = &bins[i];
        bins[i].header.size = 0;
    }

    size_t start_addr = (size_t)__heap_start;
    size_t aligned_start = ALIGN(start_addr);

    if (aligned_start + MIN_BLOCK_SIZE > (size_t)__heap_end) return;

    header_t *first_block = (header_t *)aligned_start;
    size_t size = ((size_t)__heap_end - aligned_start) & SIZE_MASK;

    if (size < MIN_BLOCK_SIZE) return;

    first_block->size = size;
    set_footer(first_block, size);

    add_to_free_list(first_block);

    heap_initialized = 1;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;
    if (!heap_initialized) init_heap();

    size_t payload_size = ALIGN(size);
    size_t required_size = ALIGN(HEADER_SIZE + payload_size + FOOTER_SIZE);
    if (required_size < MIN_BLOCK_SIZE) required_size = MIN_BLOCK_SIZE;

    int start_bin = get_bin_index(required_size);

    for (int i = start_bin; i < NUM_BINS; i++) {
        free_node_t *sentinel = &bins[i];
        free_node_t *curr = sentinel->next;

        while (curr != sentinel) {
            header_t *block = to_header(curr);
            size_t curr_size = get_size(block);

            if (curr_size >= required_size) {
                if (curr_size >= required_size + MIN_BLOCK_SIZE) {
                    size_t remaining = curr_size - required_size;

                    remove_from_free_list(block);

                    block->size = required_size | ALLOCATED_MASK;
                    set_footer(block, required_size);

                    header_t *new_block = (header_t *)((char *)block + required_size);
                    new_block->size = remaining;
                    set_footer(new_block, remaining);

                    add_to_free_list(new_block);
                } else {
                    remove_from_free_list(block);
                    block->size |= ALLOCATED_MASK;
                }

                return (char *)block + HEADER_SIZE;
            }

            curr = curr->next;
        }
    }

    return NULL;
}

void free(void *ptr) {
    if (!ptr) return;

    header_t *block = (header_t *)((char *)ptr - HEADER_SIZE);
    size_t size = get_size(block);

    block->size &= ~ALLOCATED_MASK;

    /* Coalesce next */
    header_t *next_block = (header_t *)((char *)block + size);
    if ((char *)next_block < __heap_end) {
         if (!is_allocated(next_block)) {
             remove_from_free_list(next_block);
             size += get_size(next_block);
             block->size = size;
             set_footer(block, size);
         }
    }

    /* Coalesce prev */
    if ((char *)block > __heap_start) {
        footer_t *prev_footer = (footer_t *)((char *)block - FOOTER_SIZE);
        if ((size_t)prev_footer >= (size_t)__heap_start) {
             size_t prev_size = prev_footer->size & SIZE_MASK;
             if (prev_size > 0) {
                 header_t *prev_block = (header_t *)((char *)block - prev_size);
                 if (!is_allocated(prev_block)) {
                     remove_from_free_list(prev_block);
                     prev_block->size += size;
                     set_footer(prev_block, prev_block->size);
                     block = prev_block;
                 }
             }
        }
    }

    add_to_free_list(block);
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    if (nmemb != 0 && total / nmemb != size) return NULL;

    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    header_t *block = (header_t *)((char *)ptr - HEADER_SIZE);
    size_t current_size = get_size(block);
    size_t payload_capacity = current_size - HEADER_SIZE - FOOTER_SIZE;

    if (payload_capacity >= size) return ptr;

    size_t needed_total = ALIGN(HEADER_SIZE + ALIGN(size) + FOOTER_SIZE);
    header_t *next_block = (header_t *)((char *)block + current_size);

    if ((char *)next_block < __heap_end && !is_allocated(next_block)) {
        size_t next_size = get_size(next_block);
        if (current_size + next_size >= needed_total) {
            remove_from_free_list(next_block);

            size_t new_total = current_size + next_size;
            block->size = new_total | ALLOCATED_MASK;
            set_footer(block, new_total);

             if (new_total >= needed_total + MIN_BLOCK_SIZE) {
                 size_t remaining = new_total - needed_total;
                 block->size = needed_total | ALLOCATED_MASK;
                 set_footer(block, needed_total);

                 header_t *split_block = (header_t *)((char *)block + needed_total);
                 split_block->size = remaining;
                 set_footer(split_block, remaining);
                 add_to_free_list(split_block);
             }

             return ptr;
        }
    }

    void *new_ptr = malloc(size);
    if (new_ptr) {
        size_t copy_size = payload_capacity < size ? payload_capacity : size;
        memcpy(new_ptr, ptr, copy_size);
        free(ptr);
    }
    return new_ptr;
}
