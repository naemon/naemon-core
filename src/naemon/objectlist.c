#include "objectlist.h"

int compare_objects(const void *a, const void *b, void *user_data)
{
	return memcmp(a, b, *(size_t *)(user_data));
}

/* adds a object to a list of objects */
int add_object_to_objectlist(objectlist **list, void *object_ptr)
{
	objectlist *temp_item = NULL;
	objectlist *new_item = NULL;

	if (list == NULL || object_ptr == NULL)
		return ERROR;

	/* skip this object if its already in the list */
	for (temp_item = *list; temp_item; temp_item = temp_item->next) {
		if (temp_item->object_ptr == object_ptr)
			break;
	}
	if (temp_item)
		return OK;

	/* allocate memory for a new list item */
	new_item = nm_malloc(sizeof(objectlist));
	/* initialize vars */
	new_item->object_ptr = object_ptr;

	/* add new item to head of list */
	new_item->next = *list;
	*list = new_item;

	return OK;
}


/* useful when we don't care if the object is unique or not */
int prepend_object_to_objectlist(objectlist **list, void *object_ptr)
{
	objectlist *item;
	if (list == NULL || object_ptr == NULL)
		return ERROR;
	item = nm_malloc(sizeof(*item));
	item->next = *list;
	item->object_ptr = object_ptr;
	*list = item;
	return OK;
}


/* useful for adding dependencies to master objects */
int prepend_unique_object_to_objectlist_ptr(objectlist **list, void *object_ptr, int (*comparator)(const void *a, const void *b, void *user_data), void *user_data)
{
	objectlist *l;
	if (list == NULL || object_ptr == NULL)
		return ERROR;
	for (l = *list; l; l = l->next) {
		if (!comparator(l->object_ptr, object_ptr, user_data))
			return OBJECTLIST_DUPE;
	}
	return prepend_object_to_objectlist(list, object_ptr);
}

static int comparator_helper(const void *a, const void *b, void *user_data)
{
	return ((int (*)(const void *, const void *))user_data)(a, b);
}

int prepend_unique_object_to_objectlist(objectlist **list, void *object_ptr, int (*comparator)(const void *a, const void *b))
{
	return prepend_unique_object_to_objectlist_ptr(list, object_ptr, *comparator_helper, comparator);
}

/* remove pointer from objectlist */
int remove_object_from_objectlist(objectlist **list, void *object_ptr) {
	objectlist *item, *next, *prev;

	if (list == NULL || object_ptr == NULL)
		return ERROR;

	for (prev = NULL, item = *list; item; prev = item, item = next) {
		next = item->next;
		if (item->object_ptr == object_ptr) {
			if (prev)
				prev->next = next;
			else
				*list = next;
			nm_free(item);
			item = prev;
		}
	}
	return OK;
}

/* frees memory allocated to a temporary object list */
int free_objectlist(objectlist **temp_list)
{
	objectlist *this_objectlist = NULL;
	objectlist *next_objectlist = NULL;

	if (temp_list == NULL)
		return ERROR;

	/* free memory allocated to object list */
	for (this_objectlist = *temp_list; this_objectlist != NULL; this_objectlist = next_objectlist) {
		next_objectlist = this_objectlist->next;
		nm_free(this_objectlist);
	}

	*temp_list = NULL;

	return OK;
}
