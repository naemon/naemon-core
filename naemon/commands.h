#ifndef _COMMANDS_H
#define _COMMANDS_H

int open_command_file(void);					/* creates the external command file as a named pipe (FIFO) and opens it for reading */
int close_command_file(void);					/* closes and deletes the external command file (FIFO) */

int process_external_command1(char *);                  /* top-level external command processor */
int process_external_command2(int, time_t, char *);	/* process an external command */
int process_external_commands_from_file(char *, int);   /* process external commands in a file */
int process_host_command(int, time_t, char *);          /* process an external host command */
int process_hostgroup_command(int, time_t, char *);     /* process an external hostgroup command */
int process_service_command(int, time_t, char *);       /* process an external service command */
int process_servicegroup_command(int, time_t, char *);  /* process an external servicegroup command */
int process_contact_command(int, time_t, char *);       /* process an external contact command */
int process_contactgroup_command(int, time_t, char *);  /* process an external contactgroup command */

int cmd_add_comment(int, time_t, char *);				/* add a service or host comment */
int cmd_delete_comment(int, char *);				/* delete a service or host comment */
int cmd_delete_all_comments(int, char *);			/* delete all comments associated with a host or service */
int cmd_delay_notification(int, char *);				/* delay a service or host notification */
int cmd_schedule_check(int, char *);				/* schedule an immediate or delayed host check */
int cmd_schedule_host_service_checks(int, char *, int);		/* schedule an immediate or delayed checks of all services on a host */
int cmd_signal_process(int, char *);				/* schedules a program shutdown or restart */
int cmd_process_service_check_result(int, time_t, char *);	/* processes a passive service check */
int cmd_process_host_check_result(int, time_t, char *);		/* processes a passive host check */
int cmd_acknowledge_problem(int, char *);			/* acknowledges a host or service problem */
int cmd_remove_acknowledgement(int, char *);			/* removes a host or service acknowledgement */
int cmd_schedule_downtime(int, time_t, char *);                 /* schedules host or service downtime */
int cmd_delete_downtime(int, char *);				/* cancels active/pending host or service scheduled downtime */
int cmd_change_object_int_var(int, char *);                     /* changes host/svc (int) variable */
int cmd_change_object_char_var(int, char *);			/* changes host/svc (char) variable */
int cmd_change_object_custom_var(int, char *);                  /* changes host/svc custom variable */
int cmd_process_external_commands_from_file(int, char *);       /* process external commands from a file */
int cmd_delete_downtime_by_start_time_comment(int, char *);
int cmd_delete_downtime_by_host_name(int, char *);
int cmd_delete_downtime_by_hostgroup_name(int, char *);

int process_passive_service_check(time_t, char *, char *, int, char *);
int process_passive_host_check(time_t, char *, int, char *);

/* Internal Command Implementations */

void disable_service_checks(service *);			/* disables a service check */
void enable_service_checks(service *);			/* enables a service check */
void enable_all_notifications(void);                    /* enables notifications on a program-wide basis */
void disable_all_notifications(void);                   /* disables notifications on a program-wide basis */
void enable_service_notifications(service *);		/* enables service notifications */
void disable_service_notifications(service *);		/* disables service notifications */
void enable_host_notifications(host *);			/* enables host notifications */
void disable_host_notifications(host *);		/* disables host notifications */
void enable_and_propagate_notifications(host *, int, int, int, int);	/* enables notifications for all hosts and services beyond a given host */
void disable_and_propagate_notifications(host *, int, int, int, int);	/* disables notifications for all hosts and services beyond a given host */
void schedule_and_propagate_downtime(host *, time_t, char *, char *, time_t, time_t, int, unsigned long, unsigned long); /* schedules downtime for all hosts beyond a given host */
void acknowledge_host_problem(host *, char *, char *, int, int, int);	/* acknowledges a host problem */
void acknowledge_service_problem(service *, char *, char *, int, int, int);	/* acknowledges a service problem */
void remove_host_acknowledgement(host *);		/* removes a host acknowledgement */
void remove_service_acknowledgement(service *);		/* removes a service acknowledgement */
void start_executing_service_checks(void);		/* starts executing service checks */
void stop_executing_service_checks(void);		/* stops executing service checks */
void start_accepting_passive_service_checks(void);	/* starts accepting passive service check results */
void stop_accepting_passive_service_checks(void);	/* stops accepting passive service check results */
void enable_passive_service_checks(service *);	        /* enables passive service checks for a particular service */
void disable_passive_service_checks(service *);         /* disables passive service checks for a particular service */
void start_using_event_handlers(void);			/* enables event handlers on a program-wide basis */
void stop_using_event_handlers(void);			/* disables event handlers on a program-wide basis */
void enable_service_event_handler(service *);		/* enables the event handler for a particular service */
void disable_service_event_handler(service *);		/* disables the event handler for a particular service */
void enable_host_event_handler(host *);			/* enables the event handler for a particular host */
void disable_host_event_handler(host *);		/* disables the event handler for a particular host */
void enable_host_checks(host *);			/* enables checks of a particular host */
void disable_host_checks(host *);			/* disables checks of a particular host */
void start_obsessing_over_service_checks(void);		/* start obsessing about service check results */
void stop_obsessing_over_service_checks(void);		/* stop obsessing about service check results */
void start_obsessing_over_host_checks(void);		/* start obsessing about host check results */
void stop_obsessing_over_host_checks(void);		/* stop obsessing about host check results */
void enable_service_freshness_checks(void);		/* enable service freshness checks */
void disable_service_freshness_checks(void);		/* disable service freshness checks */
void enable_host_freshness_checks(void);		/* enable host freshness checks */
void disable_host_freshness_checks(void);		/* disable host freshness checks */
void enable_performance_data(void);                     /* enables processing of performance data on a program-wide basis */
void disable_performance_data(void);                    /* disables processing of performance data on a program-wide basis */
void start_executing_host_checks(void);			/* starts executing host checks */
void stop_executing_host_checks(void);			/* stops executing host checks */
void start_accepting_passive_host_checks(void);		/* starts accepting passive host check results */
void stop_accepting_passive_host_checks(void);		/* stops accepting passive host check results */
void enable_passive_host_checks(host *);	        /* enables passive host checks for a particular host */
void disable_passive_host_checks(host *);         	/* disables passive host checks for a particular host */
void start_obsessing_over_service(service *);		/* start obsessing about specific service check results */
void stop_obsessing_over_service(service *);		/* stop obsessing about specific service check results */
void start_obsessing_over_host(host *);			/* start obsessing about specific host check results */
void stop_obsessing_over_host(host *);			/* stop obsessing about specific host check results */
void set_host_notification_number(host *, int);		/* sets current notification number for a specific host */
void set_service_notification_number(service *, int);	/* sets current notification number for a specific service */
void enable_contact_host_notifications(contact *);      /* enables host notifications for a specific contact */
void disable_contact_host_notifications(contact *);     /* disables host notifications for a specific contact */
void enable_contact_service_notifications(contact *);   /* enables service notifications for a specific contact */
void disable_contact_service_notifications(contact *);  /* disables service notifications for a specific contact */

int launch_command_file_worker(void);
int shutdown_command_file_worker(void);
int disconnect_command_file_worker(void);

#endif
