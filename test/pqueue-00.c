
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../src/pqueue.h"
#include "../src/pqueue.c"

struct entry {
        int add;
        int pri;
        int pos;
};

static int entry_compare (void *a, void *b)
{
        struct entry *ea = (struct entry *) a;
        struct entry *eb = (struct entry *) b;
        if (ea->pri < eb->pri) {
                return -1;
        }
        if (ea->pri > eb->pri) {
                return 1;
        }
        return 0;
}

static void entry_set_position (void *a, unsigned int p)
{
        struct entry *ea = (struct entry *) a;
        ea->pos = p;
}

static unsigned int entry_get_position (void *a)
{
        struct entry *ea = (struct entry *) a;
        return ea->pos;
}

int main (int argc, char *argv[])
{
        int i;
        int count;
        struct entry *entries;

        int p;
        struct entry *entry;
        struct medusa_pqueue_head *pqueue;

        long int seed;

        (void) argc;
        (void) argv;

        seed = time(NULL);
        srand(seed);

        fprintf(stderr, "seed: %ld\n", seed);

        count = rand() % 10000;
        entries = malloc(sizeof(struct entry) * count);
        if (entries == NULL) {
                return -1;
        }
        for (i = 0; i < count; i++) {
                entries[i].add = 0;
                entries[i].pri = i;
        }

        pqueue = medusa_pqueue_create(0, rand() % 64, entry_compare, entry_set_position, entry_get_position);
        if (pqueue == NULL) {
                return -1;
        }

        fprintf(stderr, "add\n");
        while (medusa_pqueue_count(pqueue) != (unsigned int) count) {
                i = rand() % count;
                if (entries[i].add == 0) {
                        medusa_pqueue_add(pqueue, &entries[i]);
                        entries[i].add = 1;
                        fprintf(stderr, "  %d = %d @ %d\n", i, entries[i].pri, entries[i].pos);
                }
        }
        if (!medusa_pqueue_verify(pqueue)) {
                return -1;
        }

        fprintf(stderr, "pop\n");
        for (p = -1, i = 0; i < count; i++) {
                entry = medusa_pqueue_pop(pqueue);
                if (entry == NULL) {
                        fprintf(stderr, "entry is invalid\n");
                        return -1;
                }
                fprintf(stderr, "  %d = %d @ %d\n", i, entries[i].pri, entries[i].pos);
                if (entry->pri <= p) {
                        fprintf(stderr, "  %d <= %d\n", entry->pri, p);
                        return -1;
                }
                p = entry->pri;
        }
        entry = medusa_pqueue_pop(pqueue);
        if (entry != NULL) {
                return -1;
        }

        medusa_pqueue_destroy(pqueue);
        free(entries);

        fprintf(stderr, "finish\n");

        return 0;
}
