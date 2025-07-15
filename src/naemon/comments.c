#include <string.h>
#include "utils.h"
#include "logging.h"
#include "config.h"
#include "common.h"
#include "comments.h"
#include "objects.h"
#include "broker.h"
#include "events.h"
#include "globals.h"
#include "nm_alloc.h"
#include <glib.h>

GHashTable *comment_hashtable = NULL;


/******************************************************************/
/**************** INITIALIZATION/CLEANUP FUNCTIONS ****************/
/******************************************************************/

/* initializes comment data */
int initialize_comment_data(void)
{
	comment_hashtable = g_hash_table_new(g_direct_hash, g_direct_equal);
	next_comment_id = 1;
	return OK;
}


/******************************************************************/
/****************** COMMENT OUTPUT FUNCTIONS **********************/
/******************************************************************/

/* adds a new host or service comment */
int add_new_comment(int type, int entry_type, char *host_name, char *svc_description, time_t entry_time, char *author_name, char *comment_data, int persistent, int source, int expires, time_t expire_time, unsigned long *comment_id)
{
	/*
	 * Regarding the "expires" field.
	 *
	 * If expires==TRUE, a event to remove the comment was created earlier. This
	 * didn't happen, since expires was never set to anything else than FALSE.
	 *
	 * If expires was set to TRUE in the retention data, the comment still
	 * didn't pass through this method, making the expires-functionality borken
	 * anyway.
	 *
	 * Therefore: Treat expires functionality just as a API-compatiblity parameter
	 * for now, but the expires-feature is removed.
	 */

	int result;

	if (type == HOST_COMMENT)
		result = add_new_host_comment(entry_type, host_name, entry_time, author_name, comment_data, persistent, source, expires, expire_time, comment_id);
	else
		result = add_new_service_comment(entry_type, host_name, svc_description, entry_time, author_name, comment_data, persistent, source, expires, expire_time, comment_id);

	return result;
}

static unsigned long get_next_comment_id(void)
{
	unsigned long new_id = next_comment_id;
	for (;;) {
		if (!find_comment(new_id, HOST_COMMENT | SERVICE_COMMENT)) {
			return new_id;
		}
		new_id++;
	}
	return 0;
}

/* adds a new host comment */
int add_new_host_comment(int entry_type, char *host_name, time_t entry_time, char *author_name, char *comment_data, int persistent, int source, int expires, time_t expire_time, unsigned long *comment_id)
{
	int result = OK;

	if (!find_host(host_name)) {
		nm_log(NSLOG_RUNTIME_ERROR, "Error: Ignoring request to add comment to non-existing host '%s'.\n", host_name);
		return ERROR;
	}

	/* find the next valid comment id */
	next_comment_id = get_next_comment_id();

	/* add comment to list in memory */
	add_host_comment(entry_type, host_name, entry_time, author_name, comment_data, next_comment_id, persistent, expires, expire_time, source);

	if (comment_id != NULL)
		*comment_id = next_comment_id;

	broker_comment_data(NEBTYPE_COMMENT_ADD, NEBFLAG_NONE, NEBATTR_NONE, HOST_COMMENT, entry_type, host_name, NULL, entry_time, author_name, comment_data, persistent, source, expires, expire_time, next_comment_id);

	/* increment the comment id AFTER broker_comment_data(),
	 * as we use it in that call
	 */
	next_comment_id++;

	return result;
}


/* adds a new service comment */
int add_new_service_comment(int entry_type, char *host_name, char *svc_description, time_t entry_time, char *author_name, char *comment_data, int persistent, int source, int expires, time_t expire_time, unsigned long *comment_id)
{
	if (!find_service(host_name, svc_description)) {
		nm_log(NSLOG_RUNTIME_ERROR, "Error: Ignoring request to add comment to non-existing service '%s' on host '%s'\n", svc_description, host_name);
		return ERROR;
	}

	/* find the next valid comment id */
	next_comment_id = get_next_comment_id();

	/* add comment to list in memory */
	add_service_comment(entry_type, host_name, svc_description, entry_time, author_name, comment_data, next_comment_id, persistent, expires, expire_time, source);

	if (comment_id != NULL)
		*comment_id = next_comment_id;

	broker_comment_data(NEBTYPE_COMMENT_ADD, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_COMMENT, entry_type, host_name, svc_description, entry_time, author_name, comment_data, persistent, source, expires, expire_time, next_comment_id);

	/* increment the comment id AFTER broker_comment_data(),
	 * as we use it in that call
	 */
	next_comment_id++;

	return OK;
}


/******************************************************************/
/***************** COMMENT DELETION FUNCTIONS *********************/
/******************************************************************/

/* deletes a host or service comment */
int delete_comment(int type, unsigned long comment_id)
{
	comment *this_comment = NULL;

	/* find the comment we should remove */
	this_comment = find_comment(comment_id, type);
	if (this_comment == NULL)
		return ERROR;

	broker_comment_data(NEBTYPE_COMMENT_DELETE, NEBFLAG_NONE, NEBATTR_NONE, type, this_comment->entry_type, this_comment->host_name, this_comment->service_description, this_comment->entry_time, this_comment->author, this_comment->comment_data, this_comment->persistent, this_comment->source, this_comment->expires, this_comment->expire_time, comment_id);

	/* remove the comment from the list in memory */
	g_hash_table_remove(comment_hashtable, GINT_TO_POINTER(this_comment->comment_id));

	// remove from svc or host
	if (type == HOST_COMMENT) {
		host *temp_host = find_host(this_comment->host_name);
		remove_object_from_objectlist(&temp_host->comments_list, this_comment);
	}
	else if (type == SERVICE_COMMENT) {
		service *temp_service = find_service(this_comment->host_name, this_comment->service_description);
		remove_object_from_objectlist(&temp_service->comments_list, this_comment);
	}

	nm_free(this_comment->host_name);
	nm_free(this_comment->service_description);
	nm_free(this_comment->author);
	nm_free(this_comment->comment_data);
	nm_free(this_comment);

	return OK;
}


/* deletes a host comment */
int delete_host_comment(unsigned long comment_id)
{
	int result = OK;

	/* delete the comment from memory */
	result = delete_comment(HOST_COMMENT, comment_id);

	return result;
}


/* deletes a service comment */
int delete_service_comment(unsigned long comment_id)
{
	int result = OK;

	/* delete the comment from memory */
	result = delete_comment(SERVICE_COMMENT, comment_id);

	return result;
}


/* deletes all comments for a particular host */
int delete_all_host_comments(host *hst)
{
	objectlist *temp_obj, *next = NULL;
	comment * temp_comment = NULL;

	/* delete host comments from memory */
	for (temp_obj = hst->comments_list; temp_obj != NULL; ) {
		next = temp_obj->next;
		temp_comment = temp_obj->object_ptr;
		delete_comment(HOST_COMMENT, temp_comment->comment_id);
		temp_obj = next;
	}

	return OK;
}


/* deletes all non-persistent acknowledgement comments for a particular host */
int delete_host_acknowledgement_comments(host *hst)
{
	objectlist *temp_obj, *next = NULL;
	comment * temp_comment = NULL;

	/* delete host comments from memory */
	for (temp_obj = hst->comments_list; temp_obj != NULL;) {
		next = temp_obj->next;
		temp_comment = temp_obj->object_ptr;
		if (temp_comment->comment_type == HOST_COMMENT && temp_comment->entry_type == ACKNOWLEDGEMENT_COMMENT && temp_comment->persistent == FALSE)
			delete_comment(HOST_COMMENT, temp_comment->comment_id);
		temp_obj = next;
	}

	return OK;
}


/* deletes all comments for a particular service */
int delete_all_service_comments(service *svc)
{
	objectlist *temp_obj, *next = NULL;
	comment * temp_comment = NULL;

	/* delete service comments from memory */
	for (temp_obj = svc->comments_list; temp_obj != NULL;) {
		next = temp_obj->next;
		temp_comment = temp_obj->object_ptr;
		delete_comment(SERVICE_COMMENT, temp_comment->comment_id);
		temp_obj = next;
	}

	return OK;
}


/* deletes all non-persistent acknowledgement comments for a particular service */
int delete_service_acknowledgement_comments(service *svc)
{
	objectlist *temp_obj, *next = NULL;
	comment * temp_comment = NULL;

	/* delete comments from memory */
	for (temp_obj = svc->comments_list; temp_obj != NULL;) {
		next = temp_obj->next;
		temp_comment = temp_obj->object_ptr;
		if (temp_comment->comment_type == SERVICE_COMMENT && temp_comment->entry_type == ACKNOWLEDGEMENT_COMMENT && temp_comment->persistent == FALSE)
			delete_comment(SERVICE_COMMENT, temp_comment->comment_id);
		temp_obj = next;
	}

	return OK;
}


/******************************************************************/
/******************** ADDITION FUNCTIONS **************************/
/******************************************************************/

/* adds a host comment to the list in memory */
int add_host_comment(int entry_type, char *host_name, time_t entry_time, char *author, char *comment_data, unsigned long comment_id, int persistent, int expires, time_t expire_time, int source)
{
	int result = OK;

	result = add_comment(HOST_COMMENT, entry_type, host_name, NULL, entry_time, author, comment_data, comment_id, persistent, expires, expire_time, source);

	return result;
}


/* adds a service comment to the list in memory */
int add_service_comment(int entry_type, char *host_name, char *svc_description, time_t entry_time, char *author, char *comment_data, unsigned long comment_id, int persistent, int expires, time_t expire_time, int source)
{
	int result = OK;

	result = add_comment(SERVICE_COMMENT, entry_type, host_name, svc_description, entry_time, author, comment_data, comment_id, persistent, expires, expire_time, source);

	return result;
}


/* adds a comment to the list in memory */
int add_comment(int comment_type, int entry_type, char *host_name, char *svc_description, time_t entry_time, char *author, char *comment_data, unsigned long comment_id, int persistent, int expires, time_t expire_time, int source)
{
	comment *new_comment = NULL;
	host *temp_host = NULL;
	service *temp_service = NULL;

	/* make sure we have the data we need */
	if (host_name == NULL || author == NULL || comment_data == NULL || (comment_type == SERVICE_COMMENT && svc_description == NULL))
		return ERROR;

	if (comment_type == HOST_COMMENT) {
		temp_host = find_host(host_name);
		if(temp_host == NULL)
			return ERROR;
	}
	else if (comment_type == SERVICE_COMMENT) {
		temp_service = find_service(host_name, svc_description);
		if(temp_service == NULL)
			return ERROR;
	}

	/* allocate memory for the comment */
	new_comment = nm_calloc(1, sizeof(comment));

	/* duplicate vars */
	new_comment->host_name = nm_strdup(host_name);
	if (comment_type == SERVICE_COMMENT) {
		new_comment->service_description = nm_strdup(svc_description);
	}
	new_comment->author = nm_strdup(author);
	new_comment->comment_data = nm_strdup(comment_data);

	new_comment->comment_type = comment_type;
	new_comment->entry_type = entry_type;
	new_comment->source = source;
	new_comment->entry_time = entry_time;
	new_comment->comment_id = comment_id;
	new_comment->persistent = (persistent == TRUE) ? TRUE : FALSE;
	new_comment->expires = (expires == TRUE) ? TRUE : FALSE;
	new_comment->expire_time = expire_time;

	g_hash_table_insert(comment_hashtable, GINT_TO_POINTER(new_comment->comment_id), new_comment);

	if (comment_type == HOST_COMMENT)
		prepend_object_to_objectlist(&temp_host->comments_list, (void *)new_comment);

	if (comment_type == SERVICE_COMMENT)
		prepend_object_to_objectlist(&temp_service->comments_list, (void *)new_comment);

	broker_comment_data(NEBTYPE_COMMENT_LOAD, NEBFLAG_NONE, NEBATTR_NONE, comment_type, entry_type, host_name, svc_description, entry_time, author, comment_data, persistent, source, expires, expire_time, comment_id);

	return OK;
}


/******************************************************************/
/********************* CLEANUP FUNCTIONS **************************/
/******************************************************************/

/* frees memory allocated for the comment data */
void free_comment_data(void)
{
	GHashTableIter iter;
	gpointer comment_;

	if(comment_hashtable == NULL)
		return;

	/* free memory for the comment list */
	g_hash_table_iter_init(&iter, comment_hashtable);
	while (g_hash_table_iter_next(&iter, NULL, &comment_)) {
		comment *temp_comment = comment_;
		nm_free(temp_comment->host_name);
		nm_free(temp_comment->service_description);
		nm_free(temp_comment->author);
		nm_free(temp_comment->comment_data);
		nm_free(temp_comment);
	}

	g_hash_table_destroy(comment_hashtable);
	comment_hashtable = NULL;

	return;
}


/******************************************************************/
/********************* UTILITY FUNCTIONS **************************/
/******************************************************************/

/* get the number of comments associated with a particular host */
int number_of_host_comments(char *host_name)
{
	objectlist *temp_obj = NULL;
	host * temp_host = NULL;
	int total_comments = 0;

	if (host_name == NULL)
		return 0;

	temp_host = find_host(host_name);
	if (temp_host == NULL)
		return 0;

	for (temp_obj = temp_host->comments_list; temp_obj != NULL; temp_obj = temp_obj->next)
		total_comments++;

	return total_comments;
}


/* get the number of comments associated with a particular service */
int number_of_service_comments(char *host_name, char *svc_description)
{
	objectlist *temp_obj = NULL;
	service *temp_service = NULL;
	int total_comments = 0;

	if (host_name == NULL || svc_description == NULL)
		return 0;

	temp_service = find_service(host_name, svc_description);
	if (temp_service == NULL)
		return 0;

	for (temp_obj = temp_service->comments_list; temp_obj != NULL; temp_obj = temp_obj->next)
		total_comments++;

	return total_comments;
}

/* get the total number of comments */
int number_of_comments()
{
	 return (int)g_hash_table_size(comment_hashtable);
}

/******************************************************************/
/********************** SEARCH FUNCTIONS **************************/
/******************************************************************/

/* find a service comment by id */
comment *find_service_comment(unsigned long comment_id)
{
	return find_comment(comment_id, SERVICE_COMMENT);
}


/* find a host comment by id */
comment *find_host_comment(unsigned long comment_id)
{
	return find_comment(comment_id, HOST_COMMENT);
}


/* find a comment by id */
comment *find_comment(unsigned long comment_id, int comment_type)
{
	comment *temp_comment = NULL;

	temp_comment = g_hash_table_lookup(comment_hashtable, GINT_TO_POINTER(comment_id));
	if (temp_comment && (temp_comment->comment_type & comment_type))
		return temp_comment;

	return NULL;
}
