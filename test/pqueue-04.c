
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../src/pqueue.h"
#include "../src/pqueue.c"

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

struct search_param {
        int check;
        int hits;
};

static int entry_search (void *context, void *a)
{
        struct entry *ea = (struct entry *) a;
        struct search_param *param = context;
        if (param->check < ea->pri) {
                return -1;
        }
        param->hits += 1;
        return 0;
}

int main (int argc, char *argv[])
{
        int i;
        int count;
        int check;
        int remain;
        struct entry *entries;

        int rc;
        struct entry kentry;
        struct search_param search_param;

        int p;
        struct entry *entry;
        struct medusa_pqueue_head *pqueue;

        long int seed;

        (void) argc;
        (void) argv;

        seed = time(NULL);
        srand(seed);

        count = rand() % 10000;
        check = rand() % count;
        remain = count - check;

        fprintf(stderr, "seed  : %ld\n", seed);
        fprintf(stderr, "count : %d\n", count);
        fprintf(stderr, "check : %d\n", check);
        fprintf(stderr, "remain: %d\n", remain);


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
                        entries[i].del = 0;
                }
        }
        if (!medusa_pqueue_verify(pqueue)) {
                fprintf(stderr, "pqueue is invalid\n");
                return -1;
        }

        fprintf(stderr, "search (<= check)\n");
        memset(&kentry, 0, sizeof(struct entry));
        kentry.pri = check;
        search_param.check = check;
        search_param.hits = 0;
        rc = medusa_pqueue_search(pqueue, &kentry, entry_search, &search_param);
        if (rc != 0) {
                fprintf(stderr, "pqueue search failed\n");
                return -1;
        }
        fprintf(stderr, "  hits: %d\n", search_param.hits);
        if (search_param.hits != check + 1) {
                fprintf(stderr, "hits is invalid\n");
                return -1;
        }

        fprintf(stderr, "pop (check)\n");
        for (p = -1, i = 0; i < check; i++) {
                entry = medusa_pqueue_pop(pqueue);
                if (entry == NULL) {
                        return -1;
                }
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
        fprintf(stderr, "pop (remain)\n");
        for (p = -1, i = 0; i < remain; i++) {
                entry = medusa_pqueue_pop(pqueue);
                if (entry == NULL) {
                        return -1;
                }
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
        entry = medusa_pqueue_pop(pqueue);
        if (entry != NULL) {
                fprintf(stderr, "pqueue is not empty\n");
                return -1;
        }

        medusa_pqueue_destroy(pqueue);
        free(entries);

        fprintf(stderr, "finish\n");

        return 0;
}
