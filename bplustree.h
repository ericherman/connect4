#ifndef __B_PLUS_TREE__
#define __B_PLUS_TREE__

#include "base.h"

#define ORDER 4
#define SPLIT_KEY_INDEX ((ORDER-1)/2)
#define SPLIT_NODE_INDEX (ORDER/2)
typedef unsigned long key_t;

#define KEY_SIZE (sizeof(key_t))

typedef union pointer {
	struct node* node_ptr;
	int value_int;
} pointer;

typedef struct record {
	key_t key;
	pointer value;
} record;

typedef struct node {
	
	struct node* parent;

	size_t num_keys; // number of entries

	// these are both 1 bigger than they can max be, but that makes
	// splitting easier (just insert, then split)
	key_t keys[ORDER];
	pointer pointers[ORDER+1]; // points to a value, or to a node

	bool is_leaf;

} node;

typedef node bpt;

bpt* new_bptree( void );
void free_bptree( bpt* b );

bpt* bpt_insert_or_update( bpt* root, record r );
record* bpt_find( bpt* root, key_t key );

void print_bpt( bpt* root, int indent );

unsigned long bpt_count_records( bpt* node );

bpt* bpt_insert_node( bpt* node, key_t up_key, bpt* sibling );
bpt* bpt_split( bpt* node );

#endif
