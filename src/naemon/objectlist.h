#ifndef _OBJECTLIST_H
#define _OBJECTLIST_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lib/lnae-utils.h"
#include "nm_alloc.h"
#include <string.h>

NAGIOS_BEGIN_DECL

typedef struct objectlist {
	void      *object_ptr;
	struct objectlist *next;
} objectlist;

#define OBJECTLIST_DUPE 1
/**
 * Append object_ptr to the list.
 * This is O(n) - you probably want to use prepend_object_to_objectlist instead.
 * This modifies the pointer you send in, so it points at the new head.
 * @param list An reference to an objectlist. Note that an empty objectlist is just NULL.
 * @param object_ptr The object you want to put in the list.
 * @returns OK if successful, ERROR otherwise
 */
int add_object_to_objectlist(struct objectlist **list, void *object_ptr);
/**
 * Put the object_ptr at the head of the list.
 * This is O(1).
 * This modifies the pointer you send in, so it points at the new head.
 * @param list An reference to an objectlist. Note that an empty objectlist is just NULL.
 * @param object_ptr The object you want to put in the list.
 * @returns OK if successful, ERROR otherwise
 */
int prepend_object_to_objectlist(struct objectlist **list, void *object_ptr);
/**
 * Put the object_ptr at the head of the list, if it isn't already in it.
 * The callback argument will be called for each existing object in the list,
 * and is expected to return 0 for any equal elements.
 * @param list An reference to an objectlist. Note that an empty objectlist is just NULL.
 * @param object_ptr The object you want to put in the list.
 * @param comparator Callback which takes two object_ptr objects, and return 0 if equal, or non-0 if not equal.
 * @returns OK if successful, OBJECTLIST_DUPE if the element was already in the list, ERROR otherwise.
 */
int prepend_unique_object_to_objectlist(objectlist **list, void *object_ptr, int (*comparator)(const void *a, const void *b));
/**
 * Put the object_ptr at the head of the list, if it isn't already in it.
 * This is similar to prepend_unique_object_to_objectlist, except it also takes
 * an arbitrary pointer that will be provided as an argument to the comparator
 * when it is invoked.
 * @param list An reference to an objectlist. Note that an empty objectlist is just NULL.
 * @param object_ptr The object you want to put in the list.
 * @param comparator Callback which takes two object_ptr objects, and return 0 if equal, or non-0 if not equal.
 * @returns OK if successful, OBJECTLIST_DUPE if the element was already in the list, ERROR otherwise.
 */
int prepend_unique_object_to_objectlist_ptr(objectlist **list, void *object_ptr, int (*comparator)(const void *a, const void *b, void *user_data), void *user_data);
/**
 * Free all the allocated memory of the objectlist. Note: this will completely
 * orphan any allocated memory inside the objectlist.
 * @param list An reference to an objectlist.
 * @returns OK if successful, ERROR otherwise
 */
int free_objectlist(objectlist **);


/**
 * A comparator using memcmp for prepend_unique_object_to_objectlist
 */
int compare_objects(const void *a, const void *b, void *user_data);

NAGIOS_END_DECL
#endif
