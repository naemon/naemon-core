/**
 * This program is useful for splitting object config based
 * on one or more hostgroups.
 * We try, wherever possible, to cache things in dependency order
 * so that all objects are cached after the objects they refer to.
 */

#ifndef NSCORE
# define NSCORE 1
#endif

#include "config.h"
#include "common.h"
#include "objects.h"
#include "comments.h"
#include "downtime.h"
#include "statusdata.h"
#include "macros.h"
#include "naemon.h"
#include "sretention.h"
#include "perfdata.h"
#include "broker.h"
#include "nebmods.h"
#include "nebmodules.h"
#include "workers.h"
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <libgen.h>

static struct {
	bitmap *hosts;
	bitmap *commands;
	bitmap *contactgroups;
	bitmap *contacts;
	bitmap *timeperiods;
	/* for including partial groups */
	bitmap *servicegroups;
	bitmap *hostgroups;
} map;

bitmap *htrack; /* tracks hosts passed to fcache_host() */

static int verbose = 1;
static const char *self_name;
static FILE *fp;
static struct object_count cached, partial;

/* remove code for simple lists with only a *next pointer */
#define nsplit_slist_remove(LSTART, LENTRY, LNEXT, LPREV) \
	do { \
		if (LPREV) \
			LPREV->next = LNEXT; \
		else \
			LSTART = LNEXT; \
		free(LENTRY); \
	} while (0)

static unsigned int ocount_total(struct object_count *c)
{
	unsigned int tot = 0, i, *ocount = (unsigned int *)c;
	for (i = 0; i < sizeof(cached) / sizeof(i); i++) {
		tot += ocount[i];
	}
	return tot;
}

static inline void nsplit_cache_command(struct command *cmd)
{
	if (!cmd || bitmap_isset(map.commands, cmd->id))
		return;

	cached.commands++;
	fcache_command(fp, cmd);
	bitmap_set(map.commands, cmd->id);
}

static int map_hostgroup_hosts(const char *hg_name)
{
	struct hostgroup *hg;
	struct hostsmember *m;

	if (!(hg = find_hostgroup(hg_name))) {
		printf("Failed to locate hostgroup '%s'\n", hg_name);
		return -1;
	}
	for (m = hg->members; m; m = m->next) {
		struct host *h = m->host_ptr;
		bitmap_set(map.hosts, h->id);
	}
	return 0;
}

static inline void nsplit_cache_timeperiod(struct timeperiod *tp)
{
	if (tp && !bitmap_isset(map.timeperiods, tp->id)) {
		bitmap_set(map.timeperiods, tp->id);
		cached.timeperiods++;
		fcache_timeperiod(fp, tp);
	}
}

static inline void nsplit_cache_hostdependencies(objectlist *olist)
{
	objectlist *list;

	for (list = olist; list; list = list->next) {
		struct hostdependency *dep = (struct hostdependency *)list->object_ptr;
		if (bitmap_isset(map.hosts, dep->master_host_ptr->id)) {
			nsplit_cache_timeperiod(dep->dependency_period_ptr);
			cached.hostdependencies++;
			fcache_hostdependency(fp, dep);
		}
	}
}

static inline void nsplit_cache_servicedependencies(objectlist *olist)
{
	objectlist *list;
	for (list = olist; list; list = list->next) {
		struct servicedependency *dep = (struct servicedependency *)list->object_ptr;
		if (!bitmap_isset(map.hosts, dep->master_service_ptr->host_ptr->id))
			continue;
		cached.servicedependencies++;
		nsplit_cache_timeperiod(dep->dependency_period_ptr);
		fcache_servicedependency(fp, dep);
	}
}

static void nsplit_cache_contacts(struct contactsmember *cm_list)
{
	struct contactsmember *cm;

	for (cm = cm_list; cm; cm = cm->next) {
		struct commandsmember *cmdm;
		struct contact *c = cm->contact_ptr;

		if (bitmap_isset(map.contacts, c->id))
			continue;
		nsplit_cache_timeperiod(c->host_notification_period_ptr);
		nsplit_cache_timeperiod(c->service_notification_period_ptr);
		for (cmdm = c->host_notification_commands; cmdm; cmdm = cmdm->next) {
			nsplit_cache_command(cmdm->command_ptr);
		}
		for (cmdm = c->service_notification_commands; cmdm; cmdm = cmdm->next) {
			nsplit_cache_command(cmdm->command_ptr);
		}
		bitmap_set(map.contacts, c->id);
		cached.contacts++;
		fcache_contact(fp, c);
	}
}

static void nsplit_cache_contactgroups(contactgroupsmember *cm)
{
	for (; cm; cm = cm->next) {
		struct contactgroup *cg = cm->group_ptr;
		if (bitmap_isset(map.contactgroups, cg->id))
			continue;
		cached.contactgroups++;
		nsplit_cache_contacts(cg->members);
		bitmap_set(map.contactgroups, cg->id);
		fcache_contactgroup(fp, cg);
	}
}

/*
 * hosts and services share a bunch of objects. we track them
 * here. Unfortunately, dependencies and escalations are still
 * separate object types, so those can't be included here
 */
#define nsplit_cache_slaves(o) \
	do { \
		nsplit_cache_command(o->event_handler_ptr); \
		nsplit_cache_command(o->check_command_ptr); \
		nsplit_cache_timeperiod(o->check_period_ptr); \
		nsplit_cache_timeperiod(o->notification_period_ptr); \
		nsplit_cache_contactgroups(o->contact_groups); \
		nsplit_cache_contacts(o->contacts); \
	} while (0)



static void nsplit_cache_host(struct host *h)
{
	struct hostsmember *parent, *next, *prev = NULL;
	struct servicesmember *sm, *sp, *sp_prev, *sp_next;
	objectlist *olist;

	if (bitmap_isset(htrack, h->id)) {
		return;
	}
	bitmap_set(htrack, h->id);
	nsplit_cache_slaves(h);

	/* massage the parent list */
	for (parent = h->parent_hosts; parent; parent = next) {
		next = parent->next;
		if (bitmap_isset(map.hosts, parent->host_ptr->id)) {
			prev = parent;
			continue;
		}
		free(parent->host_name);
		free(parent);
		if (prev)
			prev->next = next;
		else {
			h->parent_hosts = next;
		}
	}
	cached.hosts++;
	fcache_host(fp, h);
	nsplit_cache_hostdependencies(h->exec_deps);
	nsplit_cache_hostdependencies(h->notify_deps);

	for (olist = h->escalation_list; olist; olist = olist->next) {
		struct hostescalation *he = (struct hostescalation *)olist->object_ptr;
		nsplit_cache_timeperiod(he->escalation_period_ptr);
		nsplit_cache_contactgroups(he->contact_groups);
		nsplit_cache_contacts(he->contacts);
		cached.hostescalations++;
		fcache_hostescalation(fp, he);
	}

	for (sm = h->services; sm; sm = sm->next) {
		struct service *s = sm->service_ptr;
		nsplit_cache_slaves(s);
		/* remove cross-host service parents, if any */
		for (sp_prev = NULL, sp = s->parents; sp; sp_prev = sp, sp = sp_next) {
			sp_next = sp->next;
			if (!bitmap_isset(map.hosts, sp->service_ptr->host_ptr->id))
				nsplit_slist_remove(s->parents, sp, sp_next, sp_prev);
		}
		cached.services++;
		fcache_service(fp, s);
		nsplit_cache_servicedependencies(s->exec_deps);
		nsplit_cache_servicedependencies(s->notify_deps);
		for (olist = s->escalation_list; olist; olist = olist->next) {
			struct serviceescalation *se = (struct serviceescalation *)olist->object_ptr;
			nsplit_cache_timeperiod(se->escalation_period_ptr);
			nsplit_cache_contactgroups(se->contact_groups);
			nsplit_cache_contacts(se->contacts);
			cached.serviceescalations++;
			fcache_serviceescalation(fp, se);
		}
	}
}

static int nsplit_partial_groups(void)
{
	struct hostgroup *hg;
	struct servicegroup *sg;

	for (hg = hostgroup_list; hg; hg = hg->next) {
		struct hostsmember *hm, *prev = NULL, *next;
		int removed = 0;

		if (bitmap_isset(map.hostgroups, hg->id)) {
			continue;
		}
		for (hm = hg->members; hm; hm = next) {
			next = hm->next;
			if (bitmap_isset(map.hosts, hm->host_ptr->id)) {
				prev = hm;
				continue;
			}
			/* not a tracked host. Remove it */
			removed++;
			if (prev)
				prev->next = next;
			else
				hg->members = next;
			free(hm);
		}
		if (hg->members) {
			if (removed)
				partial.hostgroups++;
			else
				cached.hostgroups++;
			fcache_hostgroup(fp, hg);
		}
	}

	for (sg = servicegroup_list; sg; sg = sg->next) {
		struct servicesmember *sm, *prev = NULL, *next;
		int removed = 0;
		for (sm = sg->members; sm; sm = next) {
			next = sm->next;
			if (bitmap_isset(map.hosts, sm->service_ptr->host_ptr->id)) {
				prev = sm;
				continue;
			}
			if (prev)
				prev->next = next;
			else
				sg->members = next;
			free(sm);
		}
		if (sg->members) {
			if (removed)
				partial.servicegroups++;
			else
				cached.servicegroups++;
			fcache_servicegroup(fp, sg);
		}
	}
	return 0;
}

static int nsplit_cache_stuff(const char *orig_groups)
{
	int ngroups = 0;
	char *groups, *comma, *grp;

	if (!orig_groups)
		return EXIT_FAILURE;

	grp = groups = strdup(orig_groups);
	for (grp = groups; grp != NULL; grp = comma ? comma + 1 : NULL) {
		if ((comma = strchr(grp, ',')))
			* comma = 0;
		ngroups++;
		if (map_hostgroup_hosts(grp) < 0)
			return -1;

		/* restore the string so we can iterate it once more */
		if (comma)
			*comma = ',';
	} while (grp);

	/* from here on out, all hosts are tagged. */
	timing_point("%lu hosts mapped in %d hostgroups\n",
	             bitmap_count_set_bits(map.hosts), ngroups);
	for (grp = groups; grp != NULL; grp = comma ? comma + 1 : NULL) {
		struct hostgroup *hg;
		struct hostsmember *m;
		if ((comma = strchr(grp, ',')))
			* comma = 0;
		hg = find_hostgroup(grp);
		cached.hostgroups++;
		fcache_hostgroup(fp, hg);
		bitmap_set(map.hostgroups, hg->id);
		for (m = hg->members; m; m = m->next) {
			nsplit_cache_host(m->host_ptr);
		}
	} while (grp);

	return 0;
}

static void usage(const char *fmt, ...)
{
	printf("Usage: %s [options] </path/to/naemon.cfg>\n", self_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -g, --groups               Comma-separated list of hostgroups\n");
	printf("  -q, --quiet                Shut up about progress\n");
	printf("  -o, --outfile              Where we should write config\n");
	printf("  -f, --force                Force subcache generation\n");
	printf("\n");
	printf("Example: %s -g network1,linux -o oconf.cache\n", self_name);
	printf("will cause the hostgroups network1 and linux to be written to the\n");
	printf("file oconf.cache, along with all the objects required for oconf.cache\n");
	printf("to be a valid naemon configuration on its own.\n");
	printf("\n");
	printf("Note: This program will first try to use the object_cache_file,\n");
	printf("Then the object_precache_file, and last it will fall back to use\n");
	printf("the raw configuration files. There's no way to avoid that with this\n");
	printf("version, so just live with it\n");
	printf("If no outfile is given, we print to stdout.\n");
	printf("\n");
	if (fmt) {
		va_list ap;
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int result, warnings = 0, errors = 0;
	int c = 0;
	char *outfile = NULL;
	char *groups = NULL;
	char *cache_file = NULL;

#ifdef HAVE_GETOPT_H
	int option_index;
	struct option long_options[] = {
		{"help", no_argument, 0, 'h' },
		{"version", no_argument, 0, 'v' },
		{"quiet", no_argument, 0, 'q' },
		{"groups", required_argument, 0, 'g' },
		{"include", required_argument, 0, 'i' },
		{"outfile", required_argument, 0, 'o' },
		{"naemon-cfg", required_argument, 0, 'c' },
		{"force", no_argument, 0, 'f' },
		{ 0, 0, 0, 0 }
	};
#define getopt(a, b, c) getopt_long(a, b, c, long_options, &option_index)
#endif

	self_name = strdup(basename(argv[0]));
	/* make sure we have the correct number of command line arguments */
	if (argc < 2) {
		usage("Not enough arguments.\n");
	}

	reset_variables();

	enable_timing_point = 1;
	for (;;) {
		c = getopt(argc, argv, "hVqvfg:i:o:O:");
		if (c < 0 || c == EOF)
			break;

		switch (c) {
		case 'v': case 'V':
			printf("oconfsplit version %s", PROGRAM_VERSION);
			break;
		case 'h': case '?':
			usage(NULL);
			break;
		case 'o':
			outfile = optarg;
			break;
		case 'c':
			config_file = optarg;
			break;
		case 'g':
			groups = optarg;
			break;
		case 'q':
			verbose = 0;
			enable_timing_point = 0;
			break;
		case 'O':
			cache_file = optarg;
			break;
		case 'f':
			/* force = 1;, but ignored for now */
			break;
		default:
			usage("Unknown argument\n");
			exit(EXIT_FAILURE);
		}
	}

	if (outfile) {
		fp = fopen(outfile, "w");
		if (!fp) {
			printf("Failed to open '%s' for writing: %m\n", outfile);
			exit(EXIT_FAILURE);
		}
	} else if (groups) {
		usage("Can't cache groups without an outfile. Try again, will ya?\n");
	}

	if (!groups && !cache_file) {
		usage("No groups specified. Redo from start\n");
	}

	if (!config_file) {
		if (optind >= argc)
			config_file = strdup(get_default_config_file());
		else
			config_file = argv[optind];
	}

	if (!config_file) {
		printf("Something went horribly wrong there. What the hells...\n");
		exit(EXIT_FAILURE);
	}

	timing_point("Loading configuration...\n");

	/*
	 * config file is last argument specified.
	 * Make sure it uses an absolute path
	 */
	config_file = nspath_absolute(config_file, NULL);
	if (config_file == NULL) {
		printf("Error allocating memory.\n");
		exit(EXIT_FAILURE);
	}

	config_file_dir = nspath_absolute_dirname(config_file, NULL);

	/* never log anything anywhere */
	use_syslog = FALSE;
	log_file = NULL;
	close_log_file();

	/* we ignore errors from here */
	if (!cache_file)
		read_main_config_file(config_file);

	/* order: named cache > cache > precache > "raw" config */
	use_precached_objects = FALSE;
	if (cache_file && !access(cache_file, R_OK)) {
		use_precached_objects = TRUE;
		object_precache_file = cache_file;
	} else if (object_cache_file && !access(object_cache_file, R_OK)) {
		/* use the right one but prevent double free */
		object_precache_file = object_cache_file;
		object_cache_file = NULL;
		use_precached_objects = TRUE;
	} else if (object_precache_file && !access(object_precache_file, R_OK)) {
		use_precached_objects = TRUE;
	}
	if (verbose && use_precached_objects) {
		printf("Using cached objects from %s\n", object_precache_file);
	}

	/* read object config files */
	result = read_all_object_data(config_file);
	if (result != OK) {
		printf("  Error processing object config files. Bailing out\n");
		exit(EXIT_FAILURE);
	}

	timing_point("Config data read\n");

	/* run object pre-flight checks only */
	if (pre_flight_object_check(&warnings, &errors) != OK) {
		printf("Pre-flight check failed. Bailing out\n");
		exit(EXIT_FAILURE);
	}
	if (pre_flight_circular_check(&warnings, &errors) != OK) {
		printf("Pre-flight circular check failed. Bailing out\n");
		exit(EXIT_FAILURE);
	}
	if (cache_file && !groups) {
		if (verbose || !outfile)
			printf("%u objects check out ok\n", ocount_total(&num_objects));
		if (outfile) {
			fcache_objects(outfile);
			printf("%u objects sorted and cached to %s\n",
			       ocount_total(&num_objects), outfile);
		}
		exit(EXIT_SUCCESS);
	}

	/* create our tracker maps */
	htrack = bitmap_create(num_objects.hosts);
	map.hosts = bitmap_create(num_objects.hosts);
	map.commands = bitmap_create(num_objects.commands);
	map.timeperiods = bitmap_create(num_objects.timeperiods);
	map.contacts = bitmap_create(num_objects.contacts);
	map.contactgroups = bitmap_create(num_objects.contactgroups);
	map.hostgroups = bitmap_create(num_objects.hostgroups);

	/* global commands are always included */
	nsplit_cache_command(ochp_command_ptr);
	nsplit_cache_command(ocsp_command_ptr);
	nsplit_cache_command(global_host_event_handler_ptr);
	nsplit_cache_command(global_service_event_handler_ptr);
	nsplit_cache_command(find_command(host_perfdata_command));
	nsplit_cache_command(find_command(service_perfdata_command));
	nsplit_cache_command(find_command(host_perfdata_file_processing_command));
	nsplit_cache_command(find_command(service_perfdata_file_processing_command));

	if (nsplit_cache_stuff(groups) < 0) {
		printf("Caching failed. Bailing out\n");
		return EXIT_FAILURE;
	}
	timing_point("Done caching essentials\n");
	nsplit_partial_groups();
	timing_point("Done caching partial groups\n");
	fflush(fp);
	if (fp != stdout)
		fclose(fp);

	/* valgrind shush factor 1000 */
	cleanup();
	timing_point("Done cleaning up. Exiting\n");

	/* exit */
	printf("%u objects cached to %s\n",
	       ocount_total(&cached) + ocount_total(&partial), outfile);
	return EXIT_SUCCESS;
}
