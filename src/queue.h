
#if !defined(MEDUSA_QUEUE_H)
#define MEDUSA_QUEUE_H

#include "queue_sys.h"

/*
 * Singly-linked List missing functions.
 */
#if !defined(SLIST_EMPTY)
#define SLIST_EMPTY(head)       ((head)->slh_first == NULL)
#endif

#if !defined(SLIST_FIRST)
#define SLIST_FIRST(head)       ((head)->slh_first)
#endif

#if !defined(SLIST_FOREACH)
#define SLIST_FOREACH(var, head, field)                                         \
        for ((var) = SLIST_FIRST((head));                                       \
            (var);                                                              \
            (var) = SLIST_NEXT((var), field))
#endif

#if !defined(SLIST_FOREACH_SAFE)
#define SLIST_FOREACH_SAFE(var, head, field, tvar)                              \
        for ((var) = SLIST_FIRST((head));                                       \
            (var) && ((tvar) = SLIST_NEXT((var), field), 1);                    \
            (var) = (tvar))
#endif

#if !defined(SLIST_FOREACH_PREVPTR)
#define SLIST_FOREACH_PREVPTR(var, varp, head, field)                           \
        for ((varp) = &SLIST_FIRST((head));                                     \
            ((var) = *(varp)) != NULL;                                          \
            (varp) = &SLIST_NEXT((var), field))
#endif

#if !defined(SLIST_INIT)
#define SLIST_INIT(head)
        do {                                                                    \
                SLIST_FIRST((head)) = NULL;                                     \
        } while (0)
#endif

#if !defined(SLIST_INSERT_AFTER)
#define SLIST_INSERT_AFTER(slistelm, elm, field)
        do {                                                                    \
                SLIST_NEXT((elm), field) = SLIST_NEXT((slistelm), field);       \
                SLIST_NEXT((slistelm), field) = (elm);                          \
        } while (0)
#endif

#if !defined(SLIST_INSERT_HEAD)
#define SLIST_INSERT_HEAD(head, elm, field)
        do {                        \
                SLIST_NEXT((elm), field) = SLIST_FIRST((head));                 \
                SLIST_FIRST((head)) = (elm);                                    \
        } while (0)
#endif

#if !defined(SLIST_NEXT)
        #define SLIST_NEXT(elm, field)  ((elm)->field.sle_next)
#endif

#if !defined(SLIST_REMOVE)
#define SLIST_REMOVE(head, elm, type, field)                                    \
        do {                                                                    \
                if (SLIST_FIRST((head)) == (elm)) {                             \
                        SLIST_REMOVE_HEAD((head), field);                       \
                }                                                               \
                else {                                                          \
                        struct type *curelm = SLIST_FIRST((head));              \
                        while (SLIST_NEXT(curelm, field) != (elm))              \
                                curelm = SLIST_NEXT(curelm, field);             \
                        SLIST_REMOVE_AFTER(curelm, field);                      \
                }                                                               \
                TRASHIT((elm)->field.sle_next);                                 \
        } while (0)
#endif

/*
 * Tail queue missing functions.
 */

#if !defined(TAILQ_FOREACH_SAFE)
#define TAILQ_FOREACH_SAFE(var, head, field, next)                        \
        for ((var) = ((head)->tqh_first);                                 \
             (var) && ((next) = ((var)->field.tqe_next), 1);              \
             (var) = (next))
#endif

#if !defined(TAILQ_FOREACH_REVERSE_SAFE)
#define TAILQ_FOREACH_REVERSE_SAFE(var, head, headname, field, tvar)      \
        for ((var) = TAILQ_LAST((head), headname);                        \
             (var) && ((tvar) = TAILQ_PREV((var), headname, field), 1);   \
             (var) = (tvar))
#endif

#if !defined(TAILQ_FIRST)
#define TAILQ_FIRST(head)                ((head)->tqh_first)
#endif

#if !defined(TAILQ_LAST)
#define TAILQ_LAST(head, headname)       (*(((struct headname *)((head)->tqh_last))->tqh_last))
#endif

#if !defined(TAILQ_NEXT)
#define TAILQ_NEXT(elm, field)           ((elm)->field.tqe_next)
#endif

#if !defined(TAILQ_PREV)
#define TAILQ_PREV(elm, headname, field) (*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))
#endif

#endif
