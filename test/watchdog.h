
#if !defined(MEDUSA_TEST_WATCHDOG_H)
#define MEDUSA_TEST_WATCHDOG_H

/*
 * ends the test if it is not armed again, or disarmed with 0, within seconds.
 * name is printed when it fires, it may be NULL.
 */
void watchdog_arm (unsigned int seconds, const char *name);

#endif
