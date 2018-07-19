
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../src/pqueue.h"

struct entry {
        int add;
        int del;
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
        struct pqueue_head *pqueue;

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
                entries[i].del = 0;
                entries[i].pri = i;
                entries[i].pos = -1;
        }

        pqueue = pqueue_create(0, rand() % 64, entry_compare, entry_set_position, entry_get_position);
        if (pqueue == NULL) {
                return -1;
        }

        fprintf(stderr, "add\n");
        while (pqueue_count(pqueue) != (unsigned int) count) {
                i = rand() % count;
                if (entries[i].add == 0) {
                        pqueue_add(pqueue, &entries[i]);
                        entries[i].add = 1;
                        entries[i].del = 0;
                        fprintf(stderr, "  %d = %d\n", i, entries[i].pri);
                }
        }

        fprintf(stderr, "mod\n");
        for (i = 0; i < count; i++) {
                int pri = rand() % count;
                int opri = entries[i].pri;
                entries[i].pri = pri;
                fprintf(stderr, "  mod @ %d: %d -> %d, cmp: %d\n", i, opri, pri, pri > opri);
                pqueue_mod(pqueue, &entries[i], opri > pri);
        }

        fprintf(stderr, "pop\n");
        for (p = -1; pqueue_count(pqueue) > 0; ) {
                entry = pqueue_pop(pqueue);
                if (entry == NULL) {
                        return -1;
                }
                fprintf(stderr, "  %d @ %d\n", entry->pri, entry->pos);
                if (entry->del != 0) {
                        fprintf(stderr, "  del is invalid: %d\n", entry->del);
                        return -1;
                }
                if (entry->pri < p) {
                        fprintf(stderr, "  %d < %d\n", entry->pri, p);
                        return -1;
                }
                p = entry->pri;
        }

        entry = pqueue_pop(pqueue);
        if (entry != NULL) {
                return -1;
        }

        pqueue_destroy(pqueue);
        free(entries);

        fprintf(stderr, "finish\n");
}
