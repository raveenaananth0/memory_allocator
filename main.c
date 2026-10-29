#include <stdio.h>
#include <stddef.h>
#include <time.h>
#include <stdlib.h>

void my_free(void *ptr);

#define HEAP_SIZE (1024 * 1024)
static char heap[HEAP_SIZE];
static size_t offset = 0;

typedef struct block_header {
    size_t size;              
    int free;                
    struct block_header *next;
} block_header_t;

static block_header_t *first_block = NULL;

typedef enum {
    FIRST_FIT,
    BEST_FIT,
    WORST_FIT
} fit_strategy_t;

static fit_strategy_t current_strategy = FIRST_FIT;
static int total_allocations = 0;
static int total_frees = 0;

block_header_t *find_free_block(size_t size, block_header_t **out_last) {
    block_header_t *current = first_block;
    block_header_t *last = NULL;
    block_header_t *best = NULL;

    while (current != NULL) {
        if (current->free && current->size >= size) {
            if (current_strategy == FIRST_FIT) {
                *out_last = last;
                return current;
            } else if (current_strategy == BEST_FIT) {
                if (best == NULL || current->size < best->size) {
                    best = current;
                }
            } else if (current_strategy == WORST_FIT) {
                if (best == NULL || current->size > best->size) {
                    best = current;
                }
            }
        }
        last = current;
        current = current->next;
    }

    *out_last = last;
    return best;
}

void reset_allocator(void) {
    offset = 0;
    first_block = NULL;
    total_allocations = 0;
    total_frees = 0;
}

void *my_malloc(size_t size) {
    block_header_t *last = NULL;
    block_header_t *current = find_free_block(size, &last);

    if (current != NULL) {
        size_t remainder = current->size - size;
        if (remainder > sizeof(block_header_t) + 16) {
            block_header_t *new_block = (block_header_t *)((char *)(current + 1) + size);
            new_block->size = remainder - sizeof(block_header_t);
            new_block->free = 1;
            new_block->next = current->next;

            current->size = size;
            current->next = new_block;
        }
        current->free = 0;
        total_allocations++;
        return (void *)(current + 1);
    }

    size_t total_size = sizeof(block_header_t) + size;
    if (offset + total_size > HEAP_SIZE) {
        return NULL; // out of memory
    }

    block_header_t *header = (block_header_t *)&heap[offset];
    header->size = size;
    header->free = 0;
    header->next = NULL;
    offset += total_size;

    if (first_block == NULL) {
        first_block = header;
    } else {
        last->next = header;
    }

    total_allocations++;
    return (void *)(header + 1);
}

void *my_calloc(size_t nmemb, size_t size) {
    if (nmemb != 0 && size > (size_t)-1 / nmemb) {
        return NULL;
    }

    size_t total = nmemb * size;
    void *ptr = my_malloc(total);
    if (ptr != NULL) {
        char *bytes = (char *)ptr;
        for (size_t i = 0; i < total; i++) {
            bytes[i] = 0;
        }
    }
    return ptr;
}

void *my_realloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return my_malloc(size);
    }
    if (size == 0) {
        my_free(ptr);
        return NULL;
    }

    block_header_t *header = (block_header_t *)ptr - 1;

    if (size <= header->size) {
        return ptr;
    }

    if (header->next != NULL && header->next->free &&
        header->size + sizeof(block_header_t) + header->next->size >= size) {
        header->size += sizeof(block_header_t) + header->next->size;
        header->next = header->next->next;
        return ptr;
    }

    void *new_ptr = my_malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    char *src = (char *)ptr;
    char *dst = (char *)new_ptr;
    for (size_t i = 0; i < header->size; i++) {
        dst[i] = src[i];
    }

    my_free(ptr);
    return new_ptr;
}

void print_stats(void) {
    size_t total_allocated = 0;
    size_t total_free = 0;
    size_t largest_free_block = 0;
    int block_count = 0;

    block_header_t *current = first_block;
    while (current != NULL) {
        block_count++;
        if (current->free) {
            total_free += current->size;
            if (current->size > largest_free_block) {
                largest_free_block = current->size;
            }
        } else {
            total_allocated += current->size;
        }
        current = current->next;
    }

    double fragmentation = (total_free > 0)
        ? 1.0 - ((double)largest_free_block / (double)total_free)
        : 0.0;

    printf("\n--- Allocator Stats ---\n");
    printf("Total blocks:        %d\n", block_count);
    printf("Total allocated:     %zu bytes\n", total_allocated);
    printf("Total free:          %zu bytes\n", total_free);
    printf("Largest free block:  %zu bytes\n", largest_free_block);
    printf("Fragmentation ratio: %.2f (0 = no fragmentation, closer to 1 = more fragmented)\n", fragmentation);
    printf("Total allocations:   %d\n", total_allocations);
    printf("Total frees:         %d\n", total_frees);
    printf("-----------------------\n");
}

void run_stress_test(fit_strategy_t strategy, const char *name) {
    reset_allocator();
    current_strategy = strategy;

    #define NUM_PTRS 200
    void *ptrs[NUM_PTRS] = {0};

    srand(42);

    clock_t start = clock();

    for (int i = 0; i < 5000; i++) {
        int idx = rand() % NUM_PTRS;

        if (ptrs[idx] != NULL) {
            my_free(ptrs[idx]);
            ptrs[idx] = NULL;
        } else {
            size_t size = (rand() % 100) + 1;
            ptrs[idx] = my_malloc(size);
        }

        if (i == 2500) {
            printf("\n=== %s (mid-run snapshot, iteration 2500) ===\n", name);
            print_stats();
        }
    }

    clock_t end = clock();
    double elapsed_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;

    printf("\n=== %s (final, after all 5000 ops) ===\n", name);
    printf("Time elapsed: %.3f ms\n", elapsed_ms);
    print_stats();

    for (int i = 0; i < NUM_PTRS; i++) {
        if (ptrs[i] != NULL) {
            my_free(ptrs[i]);
        }
    }
}

void my_free(void *ptr) {
    if (ptr == NULL) return;
    total_frees++;
    block_header_t *header = (block_header_t *)ptr - 1;
    header->free = 1;

    if (header->next != NULL && header->next->free) {
        header->size += sizeof(block_header_t) + header->next->size;
        header->next = header->next->next;
    }

    block_header_t *current = first_block;
    while (current != NULL) {
        if (current->next == header && current->free) {
            current->size += sizeof(block_header_t) + header->size;
            current->next = header->next;
            break;
        }
        current = current->next;
    }
}

int main(void) {
    run_stress_test(FIRST_FIT, "First Fit");
    run_stress_test(BEST_FIT, "Best Fit");
    run_stress_test(WORST_FIT, "Worst Fit");

    return 0;
}
