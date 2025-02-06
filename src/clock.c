
#include <errno.h>
#include <time.h>

#if defined(__WINDOWS__)
#include <profileapi.h>
#endif

#include "error.h"

#if !defined(CLOCK_REALTIME_COARSE)
#define CLOCK_REALTIME_COARSE   CLOCK_REALTIME
#endif

#if !defined(CLOCK_MONOTONIC_RAW)
#define CLOCK_MONOTONIC_RAW     CLOCK_MONOTONIC
#endif

#if !defined(CLOCK_MONOTONIC_COARSE)
#define CLOCK_MONOTONIC_COARSE  CLOCK_MONOTONIC
#endif

__attribute__ ((visibility ("default"))) int medusa_clock_realtime (struct timespec *timespec)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timespec)) {
                return -EINVAL;
        }
        rc = clock_gettime(CLOCK_REALTIME, timespec);
        if (rc < 0) {
                return errno;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_clock_realtime_coarse (struct timespec *timespec)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timespec)) {
                return -EINVAL;
        }
        rc = clock_gettime(CLOCK_REALTIME_COARSE, timespec);
        if (rc < 0) {
                return errno;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_clock_monotonic (struct timespec *timespec)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timespec)) {
                return -EINVAL;
        }
        rc = clock_gettime(CLOCK_MONOTONIC, timespec);
        if (rc < 0) {
                return errno;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_clock_monotonic_raw (struct timespec *timespec)
{
        if (MEDUSA_IS_ERR_OR_NULL(timespec)) {
                return -EINVAL;
        }
#if defined(__WINDOWS__)
        LARGE_INTEGER performanceCount;
        static LARGE_INTEGER performanceFrequency = { 0 };
        if (performanceFrequency.QuadPart == 0) {
                QueryPerformanceFrequency(&performanceFrequency);
        }
        QueryPerformanceCounter(&performanceCount);
        timespec->tv_sec = performanceCount.QuadPart / performanceFrequency.QuadPart;
        timespec->tv_nsec = (performanceCount.QuadPart * 1000000000) / performanceFrequency.QuadPart;
#else
        int rc;
        rc = clock_gettime(CLOCK_MONOTONIC_RAW, timespec);
        if (rc < 0) {
                return errno;
        }
#endif
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_clock_monotonic_coarse (struct timespec *timespec)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timespec)) {
                return -EINVAL;
        }
        rc = clock_gettime(CLOCK_MONOTONIC_COARSE, timespec);
        if (rc < 0) {
                return errno;
        }
        return 0;
}
