
#if !defined(MEDUSA_PQUEUE_H)
#define MEDUSA_PQUEUE_H

struct medusa_pqueue_head;

struct medusa_pqueue_head * medusa_pqueue_create (
        unsigned int size, unsigned int step,
        int (*compare) (void *a, void *b),
        void (*setpos) (void *entry, unsigned int position),
        unsigned int (*getpos) (void *entry));
void medusa_pqueue_destroy (struct medusa_pqueue_head *head);

unsigned int medusa_pqueue_count (struct medusa_pqueue_head *head);

int medusa_pqueue_add (struct medusa_pqueue_head *head, void *entry);
int medusa_pqueue_mod (struct medusa_pqueue_head *head, void *entry, int compare);
int medusa_pqueue_del (struct medusa_pqueue_head *head, void *entry);
int medusa_pqueue_verify (struct medusa_pqueue_head *head);

void * medusa_pqueue_peek (struct medusa_pqueue_head *head);
void * medusa_pqueue_pop (struct medusa_pqueue_head *head);
int medusa_pqueue_search (struct medusa_pqueue_head *head, void *key, int (*callback) (void *context, void *entry), void *context);
int medusa_pqueue_traverse (struct medusa_pqueue_head *head, int (*callback) (void *context, void *entry), void *context);

#endif
