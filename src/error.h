
#if !defined(MEDUSA_ERROR_H)
#define MEDUSA_ERROR_H

#include <stdint.h>
#include <string.h>

#define MAX_ERRNO       4095

#define MEDUSA_IS_ERR_VALUE(x) ((uintptr_t) (void *) (x) >= (uintptr_t) -MAX_ERRNO)

static inline void * MEDUSA_ERR_PTR (int error)
{
        return (void *) (intptr_t) error;
}

static inline int MEDUSA_PTR_ERR (const void *ptr)
{
        return (int) (intptr_t) ptr;
}

static inline int MEDUSA_IS_ERR (const void *ptr)
{
        return MEDUSA_IS_ERR_VALUE((uintptr_t) ptr);
}

static inline int MEDUSA_IS_ERR_OR_NULL (const void *ptr)
{
        return (!ptr) || MEDUSA_IS_ERR_VALUE((uintptr_t) ptr);
}

static inline char * medusa_strerror (int error)
{
        if (error < 0) {
                return strerror(-error);
        } else {
                return strerror(error);
        }
}

#endif
