
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "pqueue.h"

#if !defined(MAX)
#define MAX(a, b)               (((a) > (b)) ? (a) : (b))
#endif

#define pqueue_left(i)          (2 * (i))
#define pqueue_right(i)         ((2 * (i)) + 1)
#define pqueue_parent(i)        ((i) / 2)

struct medusa_pqueue_head {
        void **entries;
        unsigned int count;
        unsigned int size;
        unsigned int step;
        int (*compare) (void *a, void *b);
        void (*setpos) (void *entry, unsigned int position);
        unsigned int (*getpos) (void *entry);
};

struct medusa_pqueue_head * medusa_pqueue_create (
        unsigned int size, unsigned int step,
        int (*compare) (void *a, void *b),
        void (*setpos) (void *entry, unsigned int position),
        unsigned int (*getpos) (void *entry))
{
        struct medusa_pqueue_head *head;
        head = malloc(sizeof(struct medusa_pqueue_head));
        if (head == NULL) {
                goto bail;
        }
        memset(head, 0, sizeof(struct medusa_pqueue_head));
        head->size = size;
        head->step = step ? step : 1;
        head->compare = compare;
        head->setpos = setpos;
        head->getpos = getpos;
        head->count = 1;
        if (head->size > 0) {
                head->entries = (void **) malloc(sizeof(void *) * head->size);
                if (head->entries == NULL) {
                        goto bail;
                }
        }
        return head;
bail:   if (head != NULL) {
                medusa_pqueue_destroy(head);
        }
        return NULL;
}

void medusa_pqueue_destroy (struct medusa_pqueue_head *head)
{
        if (head == NULL) {
                return;
        }
        if (head->entries != NULL) {
                free(head->entries);
        }
        free(head);
}

unsigned int medusa_pqueue_count (struct medusa_pqueue_head *head)
{
        return head->count -1;
}

static inline void pqueue_shift_up (struct medusa_pqueue_head *head, unsigned int i)
{
        void *e;
        unsigned int p;
        e = head->entries[i];
        p = pqueue_parent(i);
        while ((i > 1) && (head->compare(head->entries[p], e) > 0)) {
                head->entries[i] = head->entries[p];
                head->setpos(head->entries[i], i);
                i = p;
                p = pqueue_parent(i);
        }
        head->entries[i] = e;
        head->setpos(head->entries[i], i);
}

static inline void pqueue_shift_down (struct medusa_pqueue_head *head, unsigned int i)
{
        void *e;
        unsigned int c;
        e = head->entries[i];
        while (1) {
                c = pqueue_left(i);
                if (c >= head->count) {
                        break;
                }
                if ((c + 1 < head->count) &&
                    (head->compare(head->entries[c], head->entries[c + 1]) > 0)) {
                        c += 1;
                }
                if (!(head->compare(e, head->entries[c]) > 0)) {
                        break;
                }
                head->entries[i] = head->entries[c];
                head->setpos(head->entries[i], i);
                i = c;
        }
        head->entries[i] = e;
        head->setpos(e, i);
}

int medusa_pqueue_add (struct medusa_pqueue_head *head, void *entry)
{
        unsigned int i;
        if (head->count + 1 >= head->size) {
                void **tmp;
                unsigned int size;
                size = MAX(head->count + 1, head->size + head->step);
                tmp = (void **) realloc(head->entries, sizeof(void **) * size);
                if (tmp == NULL) {
                        tmp = (void **) malloc(sizeof(void *) * size);
                        if (tmp == NULL) {
                                goto bail;
                        }
                        if (head->count > 0) {
                                memcpy(tmp, head->entries, sizeof(void **) * size);
                        }
                        free(head->entries);
                }
                head->entries = tmp;
                head->size = size;
        }
        i = head->count++;
        head->entries[i] = entry;
        head->setpos(entry, i);
        pqueue_shift_up(head, i);
        return 0;
bail:   return -1;
}

int medusa_pqueue_mod (struct medusa_pqueue_head *head, void *entry, int compare)
{
        unsigned int i;
        i = head->getpos(entry);
        if (compare > 0) {
                pqueue_shift_up(head, i);
        } else {
                pqueue_shift_down(head, i);
        }
        return 0;
}

int medusa_pqueue_del (struct medusa_pqueue_head *head, void *entry)
{
        unsigned int i;
        i = head->getpos(entry);
        if (i == (unsigned int) -1) {
                goto bail;
        }
        head->entries[i] = head->entries[--head->count];
        if (head->compare(entry, head->entries[i]) > 0) {
                pqueue_shift_up(head, i);
        } else {
                pqueue_shift_down(head, i);
        }
        head->setpos(entry, -1);
        return 0;
bail:   return -1;
}

void * medusa_pqueue_peek (struct medusa_pqueue_head *head)
{
        void *e;
        if (head->count == 1) {
                return NULL;
        }
        e = head->entries[1];
        return e;
}

void * medusa_pqueue_pop (struct medusa_pqueue_head *head)
{
        void *e;
        if (head->count == 1) {
                return NULL;
        }
        e = head->entries[1];
        head->entries[1] = head->entries[--head->count];
        pqueue_shift_down(head, 1);
        head->setpos(e, -1);
        return e;
}

static int pqueue_search_actual (struct medusa_pqueue_head *head, void *key, int (*callback) (void *context, void *entry), void *context, unsigned int pos)
{
        int rc;
        if (pqueue_left(pos) < head->count) {
                if (head->compare(head->entries[pqueue_left(pos)], key) <= 0) {
                        rc = callback(context, head->entries[pqueue_left(pos)]);
                        if (rc != 0) {
                                return rc;
                        }
                        rc = pqueue_search_actual(head, key, callback, context, pqueue_left(pos));
                        if (rc != 0) {
                                return rc;
                        }
                }
        }
        if (pqueue_right(pos) < head->count) {
                if (head->compare(head->entries[pqueue_right(pos)], key) <= 0) {
                        rc = callback(context, head->entries[pqueue_right(pos)]);
                        if (rc != 0) {
                                return rc;
                        }
                        rc = pqueue_search_actual(head, key, callback, context, pqueue_right(pos));
                        if (rc != 0) {
                                return rc;
                        }
                }
        }
        return 0;
}

int medusa_pqueue_search (struct medusa_pqueue_head *head, void *key, int (*callback) (void *context, void *entry), void *context)
{
        int rc;
        if (1 < head->count) {
                if (head->compare(head->entries[1], key) <= 0) {
                        rc = callback(context, head->entries[1]);
                        if (rc != 0) {
                                return rc;
                        }
                        rc = pqueue_search_actual(head, key, callback, context, 1);
                        if (rc != 0) {
                                return rc;
                        }
                }
        }
        return 0;
}

static int pqueue_traverse_actual (struct medusa_pqueue_head *head, int (*callback) (void *context, void *entry), void *context, unsigned int pos)
{
        int rc;
        if (pqueue_left(pos) < head->count) {
                rc = callback(context, head->entries[pqueue_left(pos)]);
                if (rc != 0) {
                        return rc;
                }
                rc = pqueue_traverse_actual(head, callback, context, pqueue_left(pos));
                if (rc != 0) {
                        return rc;
                }
        }
        if (pqueue_right(pos) < head->count) {
                rc = callback(context, head->entries[pqueue_right(pos)]);
                if (rc != 0) {
                        return rc;
                }
                rc = pqueue_traverse_actual(head, callback, context, pqueue_right(pos));
                if (rc != 0) {
                        return rc;
                }
        }
        return 0;
}

int medusa_pqueue_traverse (struct medusa_pqueue_head *head, int (*callback) (void *context, void *entry), void *context)
{
        int rc;
        if (1 < head->count) {
                rc = callback(context, head->entries[1]);
                if (rc != 0) {
                        return rc;
                }
                rc = pqueue_traverse_actual(head, callback, context, 1);
                if (rc != 0) {
                        return rc;
                }
        }
        return 0;
}

static int pqueue_is_valid_actual (struct medusa_pqueue_head *head, unsigned int pos)
{
        if (pqueue_left(pos) < head->count) {
                if (head->compare(head->entries[pos], head->entries[pqueue_left(pos)]) > 0) {
                        return 0;
                }
                if (!pqueue_is_valid_actual(head, pqueue_left(pos))) {
                        return 0;
                }
        }
        if (pqueue_right(pos) < head->count) {
                if (head->compare(head->entries[pos], head->entries[pqueue_right(pos)]) > 0) {
                        return 0;
                }
                if (!pqueue_is_valid_actual(head, pqueue_right(pos))) {
                        return 0;
                }
        }
        return 1;
}

int medusa_pqueue_verify (struct medusa_pqueue_head *head)
{
        return pqueue_is_valid_actual(head, 1);
}
