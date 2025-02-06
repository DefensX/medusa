
#ifdef __cplusplus
extern "C"
{
#endif

#if !defined(MEDUSA_DEBUG_NAME)
#define MEDUSA_DEBUG_NAME        "unknown"
#endif

enum {
        MEDUSA_DEBUG_LEVEL_INVALID,
        MEDUSA_DEBUG_LEVEL_SILENT,
        MEDUSA_DEBUG_LEVEL_ERROR,
        MEDUSA_DEBUG_LEVEL_WARNING,
        MEDUSA_DEBUG_LEVEL_NOTICE,
        MEDUSA_DEBUG_LEVEL_INFO,
        MEDUSA_DEBUG_LEVEL_DEBUG,
        MEDUSA_DEBUG_LEVEL_TRACE
#define MEDUSA_DEBUG_LEVEL_INVALID    MEDUSA_DEBUG_LEVEL_INVALID
#define MEDUSA_DEBUG_LEVEL_SILENT     MEDUSA_DEBUG_LEVEL_SILENT
#define MEDUSA_DEBUG_LEVEL_ERROR      MEDUSA_DEBUG_LEVEL_ERROR
#define MEDUSA_DEBUG_LEVEL_WARNING    MEDUSA_DEBUG_LEVEL_WARNING
#define MEDUSA_DEBUG_LEVEL_NOTICE     MEDUSA_DEBUG_LEVEL_NOTICE
#define MEDUSA_DEBUG_LEVEL_INFO       MEDUSA_DEBUG_LEVEL_INFO
#define MEDUSA_DEBUG_LEVEL_DEBUG      MEDUSA_DEBUG_LEVEL_DEBUG
#define MEDUSA_DEBUG_LEVEL_TRACE      MEDUSA_DEBUG_LEVEL_TRACE
};

extern int medusa_debug_level;

#define medusa_enterf() { \
        medusa_tracef("enter"); \
}

#define medusa_leavef() { \
        medusa_tracef("leave"); \
}

#define medusa_tracef(a...) { \
        if (medusa_debug_level >= MEDUSA_DEBUG_LEVEL_TRACE) { \
                medusa_debug_printf(MEDUSA_DEBUG_LEVEL_TRACE, MEDUSA_DEBUG_NAME, __FUNCTION__, __FILE__, __LINE__, a); \
        } \
}

#define medusa_debugf(a...) { \
        if (medusa_debug_level >= MEDUSA_DEBUG_LEVEL_DEBUG) { \
                medusa_debug_printf(MEDUSA_DEBUG_LEVEL_DEBUG, MEDUSA_DEBUG_NAME, __FUNCTION__, __FILE__, __LINE__, a); \
        } \
}

#define medusa_warningf(a...) { \
        if (medusa_debug_level >= MEDUSA_DEBUG_LEVEL_WARNING) { \
                medusa_debug_printf(MEDUSA_DEBUG_LEVEL_WARNING, MEDUSA_DEBUG_NAME, __FUNCTION__, __FILE__, __LINE__, a); \
        } \
}

#define medusa_noticef(a...) { \
        if (medusa_debug_level >= MEDUSA_DEBUG_LEVEL_NOTICE) { \
                medusa_debug_printf(MEDUSA_DEBUG_LEVEL_NOTICE, MEDUSA_DEBUG_NAME, __FUNCTION__, __FILE__, __LINE__, a); \
        } \
}

#define medusa_infof(a...) { \
        if (medusa_debug_level >= MEDUSA_DEBUG_LEVEL_INFO) { \
                medusa_debug_printf(MEDUSA_DEBUG_LEVEL_INFO, MEDUSA_DEBUG_NAME, __FUNCTION__, __FILE__, __LINE__, a); \
        } \
}

#define medusa_errorf(a...) { \
        if (medusa_debug_level >= MEDUSA_DEBUG_LEVEL_ERROR) { \
                medusa_debug_printf(MEDUSA_DEBUG_LEVEL_ERROR, MEDUSA_DEBUG_NAME, __FUNCTION__, __FILE__, __LINE__, a); \
        } \
}

const char * medusa_debug_level_to_string (int level);
int medusa_debug_level_from_string (const char *string);
int medusa_debug_printf (int level, const char *name, const char *function, const char *file, int line, const char *fmt, ...) __attribute__((format(printf, 6, 7)));

void medusa_debug_lock (void);
void medusa_debug_unlock (void);

#ifdef __cplusplus
}
#endif
