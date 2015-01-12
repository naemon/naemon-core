#include "config.h"
#include "common.h"
#include "statusdata.h"
#include "broker.h"
#include "sretention.h"
#include "events.h"
#include "utils.h"
#include "checks.h"
#include "notifications.h"
#include "logging.h"
#include "globals.h"
#include "defaults.h"
#include "nm_alloc.h"
#include <math.h>
#include <string.h>
#include <time.h>

/* Which clock should be used for events? */
#define EVENT_CLOCK_ID CLOCK_MONOTONIC

struct timed_event {
	size_t pos;
	struct timespec event_time;
	event_callback callback;
	void *user_data;
};

struct timed_event_queue {
	struct timed_event **queue;
	size_t count;
	size_t size;
};

struct timed_event_queue *event_queue = NULL; /* our scheduling queue */


/******************************************************************/
/************************** TIME HELEPERS *************************/
/******************************************************************/

static inline long timespec_msdiff(struct timespec *a, struct timespec *b)
{
	long diff = 0;
	diff += a->tv_sec * 1000;
	diff -= b->tv_sec * 1000;
	diff += a->tv_nsec / 1000000;
	diff -= b->tv_nsec / 1000000;
	return diff;
}

/******************************************************************/
/************************** HEAP METHODS **************************/
/******************************************************************/

static inline int evheap_compare(struct timed_event *eva, struct timed_event *evb)
{
	if (eva->event_time.tv_sec < evb->event_time.tv_sec)
		return -1;
	if (eva->event_time.tv_sec > evb->event_time.tv_sec)
		return 1;
	if (eva->event_time.tv_nsec < evb->event_time.tv_nsec)
		return -1;
	if (eva->event_time.tv_nsec > evb->event_time.tv_nsec)
		return 1;
	return 0;
}

static void evheap_set_size(struct timed_event_queue *q, size_t new_size)
{
	size_t size = q->size;

	if(new_size < 1)
		new_size = 1;

	while (size < new_size) {
		size *= 2;
	}
	/* If to big, shrink, but have some hysteresis */
	while (size >= new_size * 3) {
		size /= 2;
	}

	if(size != q->size) {
		q->size = size;
		q->queue = nm_realloc(q->queue, q->size * sizeof(struct timed_event *));
	}
}

static int evheap_cond_swap(struct timed_event_queue *q, size_t idx_low, size_t idx_high)
{
	struct timed_event *ev_low, *ev_high;

	if(idx_low == idx_high)
		return 0;

	/* Assume we need to swap */
	ev_low = q->queue[idx_high];
	ev_high = q->queue[idx_low];

	/* If assumption isn't correct, bail out */
	if(evheap_compare(ev_high, ev_low) < 0)
		return 0;

	/* Assumption were correct, save */
	q->queue[idx_low] = ev_low;
	q->queue[idx_high] = ev_high;

	/* Update positions */
	ev_low->pos = idx_low;
	ev_high->pos = idx_high;

	return 1;
}

static void evheap_bubble_up(struct timed_event_queue *q, size_t idx)
{
	size_t parent;
	while(idx>0) {
		parent = (idx-1)>>1;
		if(!evheap_cond_swap(q, parent, idx))
			break;
		idx = parent;
	}
}

static void evheap_bubble_down(struct timed_event_queue *q, size_t idx)
{
	size_t child;
	while ((child = (idx<<1)+1) < q->count) {
		if (child+1 < q->count)
			if(evheap_compare(q->queue[child], q->queue[child+1]) > 0)
				child++;
		if(!evheap_cond_swap(q, idx, child))
			break;
		idx = child;
	}
}

static struct timed_event *evheap_head(struct timed_event_queue *q)
{
	if (q->count == 0)
		return NULL;
	return q->queue[0];
}

static void evheap_remove(struct timed_event_queue *q, struct timed_event *ev)
{
	q->queue[ev->pos] = q->queue[q->count-1];
	q->queue[ev->pos]->pos = ev->pos;

	q->count--;
	evheap_set_size(q, q->count);

	/* If it wasn't the last node, bubble */
	if (ev->pos <= q->count) {
		evheap_bubble_down(q, ev->pos);
		evheap_bubble_up(q, ev->pos);
	}
}

static void evheap_add(struct timed_event_queue *q, struct timed_event *ev)
{
	evheap_set_size(q, q->count+1);
	ev->pos = q->count;
	q->queue[ev->pos] = ev;
	q->count++;

	evheap_bubble_up(q, ev->pos);
}

static struct timed_event_queue *evheap_create(void)
{
	struct timed_event_queue *q;
	q = nm_calloc(1, sizeof(struct timed_event_queue));

	/* Since count is 0, the queue will shirnk at first insert anyway. No need to start with a bigger queue */
	q->size = 1;
	q->queue = nm_calloc(q->size, sizeof(struct timed_event *));

	q->count = 0;
	return q;
}

static void evheap_destroy(struct timed_event_queue *q) {
	/* It must be empty... TODO: verify it is empty */
	if(q==NULL)
		return;
	nm_free(q->queue);
	nm_free(q);
}



/******************************************************************/
/************ EVENT SCHEDULING/HANDLING FUNCTIONS *****************/
/******************************************************************/

timed_event *schedule_event(time_t delay, event_callback callback, void *user_data)
{
	timed_event *event = nm_calloc(1, sizeof(struct timed_event));

	clock_gettime(EVENT_CLOCK_ID, &event->event_time);
	event->event_time.tv_sec += delay;

	event->callback = callback;
	event->user_data = user_data;

	evheap_add(event_queue, event);

	return event;
}

/* Unschedule, execute and destroy event, given parameters of evprop */
static void execute_and_destroy_event(struct timed_event_properties *evprop)
{
	evheap_remove(event_queue, evprop->event);
	(*evprop->event->callback)(evprop);
	nm_free(evprop->event);
}

/* remove an event from the queue */
void destroy_event(timed_event *event)
{
	struct timed_event_properties evprop;
	evprop.event = event;
	evprop.flags = EVENT_EXEC_FLAG_ABORT;
	evprop.latency = 0.0;
	evprop.user_data = event->user_data;
	execute_and_destroy_event(&evprop);
}

void init_event_queue(void)
{
	event_queue = evheap_create();
}

void destroy_event_queue(void)
{
	struct timed_event *ev;
	/*
	 * Since naemon doesn't know if things is started, we can't trust that
	 * destroy event queue actually means we have an event queue to destroy
	 */
	if(event_queue == NULL)
		return;

	while((ev = evheap_head(event_queue)) != NULL) {
		destroy_event(ev);
	}
	evheap_destroy(event_queue);
}

/* this is the main event handler loop */
void event_execution_loop(void)
{
	timed_event *evt;
	struct timespec current_time;
	long time_diff;
	struct timed_event_properties evprop;
	int inputs;

	while (!sigshutdown && !sigrestart) {

		/* get the current time */
		clock_gettime(EVENT_CLOCK_ID, &current_time);

		if (sigrotate == TRUE) {
			sigrotate = FALSE;
			rotate_log_file(time(NULL));
			update_program_status(FALSE);
		}

		/* get next scheduled event */
		evt = evheap_head(event_queue);

		/* if we don't have any events to handle, exit */
		if (!evt) {
			log_debug_info(DEBUGL_EVENTS, 0, "There aren't any events that need to be handled! Exiting...\n");
			break;
		}

		time_diff = timespec_msdiff(&evt->event_time, &current_time);
		if (time_diff < 0)
			time_diff = 0;
		else if (time_diff >= 1500)
			time_diff = 1500;

		if (!iobroker_push(nagios_iobs)) {
			/* There was a backlog for data sending? Catch up at "idle
			 * priority", i.e. prevent sleeping while awaiting
			 * results, but continue to run events as usual.
			 */
			time_diff = 0;
		}
		inputs = iobroker_poll(nagios_iobs, time_diff);
		if (inputs < 0 && errno != EINTR) {
			nm_log(NSLOG_RUNTIME_ERROR, "Error: Polling for input on %p failed: %s", nagios_iobs, iobroker_strerror(inputs));
			break;
		}
		if (inputs < 0 ) {
			/*
			 * errno is EINTR, which means it isn't a timed event, thus don't
			 * continue below
			 */
			continue;
		}

		log_debug_info(DEBUGL_IPC, 2, "## %d descriptors had input\n", inputs);

		/*
		 * Since we got input on one of the file descriptors, this wakeup wans't
		 * about a timed event, so start the main loop over.
		 */
		if (inputs > 0) {
			log_debug_info(DEBUGL_EVENTS, 0, "Event was cancelled by iobroker input\n");
			continue;
		}

		/*
		 * Might have been a timeout just because the max time of polling
		 */
		clock_gettime(EVENT_CLOCK_ID, &current_time);
		time_diff = timespec_msdiff(&evt->event_time, &current_time);
		if (time_diff > 0)
			continue;

		/*
		 * It isn't any special cases, so it's time to run the event
		 */
		evprop.event = evt;
		evprop.flags = EVENT_EXEC_FLAG_TIMED;
		evprop.latency = -time_diff/1000.0;
		evprop.user_data = evt->user_data;
		execute_and_destroy_event(&evprop);
	}
}
