/*
 * Copyright (c) 2004, 2007 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * @file rbtree.h
 * @brief RedBlack tree library
 *
 * This library implements the algorithm for red-black trees, as
 * described at http://en.wikipedia.org/wiki/Red%E2%80%93black_tree
 * Red-Black trees provide insert, find and delete in O(log n) time
 * in O(n) space. Inorder, preorder and postorder iteration is done
 * in O(n) time.
 *
 * This implementation was originally written by Todd C. Miller
 * <todd.Miller@courtesan.com> and was taken from Apple's "sudo"
 * program. It has been modified to match the libnaemon naming
 * scheme and naemon's (future) usage of it as a replacement for
 * the horrible skiplists, which provide the same operations as
 * red-black trees, but are an order of magnitude slower in the
 * worst case; in reality, skiplists are 50% slower than red-black
 * trees, but use more memory than red-black trees to guarantee
 * that (worse) performance.
 */

#ifndef LIBNAEMON_rbtree_h__
#define LIBNAEMON_rbtree_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lnae-utils.h"

enum rbcolor {
    rbtree_red,
    rbtree_black
};

enum rbtree_traversal {
    rbpreorder,
    rbinorder,
    rbpostorder
};

struct rbnode {
	struct rbnode *left, *right, *parent;
	void *data;
	enum rbcolor color;
};

struct rbtree {
	int (*compar)(const void *, const void *);
	struct rbnode root;
	struct rbnode nil;
	unsigned int num_nodes;
};

/** Obtain the first node of the tree */
#define rbtree_first(t)		((t)->root.left)
/** Traverse an entire tree */
#define rbtree_traverse(t, f, c, o)	rbtree_traverse_node((t), rbtree_first(t), (f), (c), (o))
/** Determine if a tree is empty */
#define rbtree_isempty(t)		((t)->root.left == &(t)->nil && (t)->root.right == &(t)->nil)
/** Obtain the root of the tree */
#define rbtree_root(t)		(&(t)->root)

NAEMON_BEGIN_DECL

/**
 * Delete a node from the tree
 */
void *rbtree_delete(struct rbtree *, struct rbnode *);

/**
 * Traverse a tree from the given node
 *
 * "func" is called once for each node under the given node in
 * the given tree, being passed "cookie" every time. The order
 * is selected from rbtree_traversal enum and can be any of
 * rbpreorder, rbinorder and rbpostorder. Traversal stops when
 * "func" returns non-zero.
 *
 * @param tree Tree to traverse
 * @param node Node to start from
 * @param func Function to call for each node under "node"
 * @param void cookie Cookie to pass to "func"
 * @param rbtraversal_order rbpreorder, rbinorder or rbpostorder
 * @return Whatever the last called "func" returns.
 */
int rbtree_traverse_node(struct rbtree *tree, struct rbnode *node,
		int (*func)(void *, void *),
		void *cookie, enum rbtree_traversal);
/**
 * Find a node in the tree
 *
 * @param tree Tree to look in
 * @param key What to look for
 * @return The redblack node containing the data searched for
 */
struct rbnode *rbtree_find_node(struct rbtree *tree, void *key);

/**
 * Insert a node in the tree
 *
 * @param tree Tree to insert in
 * @param data Data to insert
 */
struct rbnode *rbtree_insert(struct rbtree *tree, void *data);

/**
 * Create a new red-black tree
 *
 * @param compar Comparison function to order the tree
 * @return The newly created tree
 */
struct rbtree *rbtree_create(int (*compar)(const void *, const void *));

/**
 * Find data in the tree
 *
 * @param tree Tree to look in
 * @param key Key to find
 * @return Found data, if any, or NULL if none can be found
 */
void *rbtree_find(struct rbtree *tree, void *key);

/**
 * Destroy a red-black tree, calling "func" for each data item
 *
 * @param tree Tree to destroy
 * @param func Destructor for the data ("free" would work")
 */
void rbtree_destroy(struct rbtree *tree, void (*func)(void *));

/**
 * Get number of data-containing nodes in the tree
 * @param tree Tree to operate on
 * @return Number of data-containing nodes
 */
unsigned int rbtree_num_nodes(struct rbtree *tree);

NAEMON_END_DECL
#endif /* LIBNAEMON_rbtree_h__ */
