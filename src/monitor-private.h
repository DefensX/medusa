
#if !defined(MEDUSA_MONITOR_PRIVATE_H)
#define MEDUSA_MONITOR_PRIVATE_H

struct medusa_subject;
struct medusa_monitor;

int medusa_monitor_lock (struct medusa_monitor *monitor);
int medusa_monitor_unlock (struct medusa_monitor *monitor);

int medusa_monitor_add (struct medusa_monitor *monitor, struct medusa_subject *subject);
int medusa_monitor_mod (struct medusa_subject *subject);
int medusa_monitor_del (struct medusa_subject *subject);

int medusa_monitor_add_unlocked (struct medusa_monitor *monitor, struct medusa_subject *subject);
int medusa_monitor_mod_unlocked (struct medusa_subject *subject);
int medusa_monitor_del_unlocked (struct medusa_subject *subject);

struct medusa_subject * medusa_monitor_get_first_subject_unlocked (struct medusa_monitor *monitor);
struct medusa_subject * medusa_monitor_get_next_subject_unlocked (struct medusa_subject *subject);

#endif
