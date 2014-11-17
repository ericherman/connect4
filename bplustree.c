#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bplustree.h"

int binary_search( int* keys, int num_keys, int target_key ) {
	
	// no data at all
	if( keys == NULL || num_keys == 0 ) {
		return -1;
	}
	
	// out of range
	if( target_key < keys[0] || target_key > keys[num_keys-1] ) {
		return -1;
	}
	
	int mid = num_keys / 2;
	int large_half;
	while( num_keys > 0 ) {

		if( target_key == keys[mid] ) {
			return mid;
		}
		
		num_keys = num_keys/2; // half the range left over
		large_half = num_keys/2 + (num_keys % 2);// being clever. But this is ceil 

		if( target_key < keys[mid] ) {
			mid -= large_half;
		} else {
			mid += large_half;
		}
		
	}

	return -1;
}

bpt* new_bptree() {
	
	bpt* out = (bpt*)malloc( sizeof(bpt) );
	if( out == NULL ) {
		perror("malloc()");
		exit( EXIT_FAILURE );
	}
	
	out->parent = NULL;
	
	out->num_keys = 0;
	out->is_leaf = true;
	
	return out;
}

void free_bptree( bpt* b ) {

//	printf("free_bptree: %p\n", b);
	if( !b->is_leaf ) {
		for( int i=0; i<=b->num_keys; i++ ) { // Note: <= since pointers is num_keys+1
			free_bptree( b->pointers[i].node_ptr );
		}
	}
	free( b );
	
}

void bpt_split( bpt** root ) {
	
	bpt* n = *root;
	printf("Split around key[%d] = %d\n", SPLIT_KEY_INDEX, n->keys[SPLIT_KEY_INDEX] );
	
	// create a sibling node and copy everything from median to end
	bpt* sibling = new_bptree();	
	memcpy( &sibling->keys[0], &n->keys[SPLIT_KEY_INDEX], sizeof(int)*RIGHT_SIZE );
	memcpy( &sibling->pointers[0], &n->pointers[SPLIT_KEY_INDEX], sizeof(pointer)*RIGHT_SIZE );
	// housekeeping
	sibling->parent = n->parent;
	sibling->num_keys = RIGHT_SIZE;
	n->num_keys = LEFT_SIZE;

	/*
	printf("Created sibling\n");
	print_bpt( sibling, 0 );
	printf("Node left over\n");
	print_bpt( n, 0 );
	*/
	
	// now insert median into our parent, along with sibling
	// but if parent is NULL, we're at the root and need to make a new one
	if( n->parent == NULL ) {
//		printf("No parent, creating new root\n");
		bpt* new_root = new_bptree();

		new_root->keys[0] = sibling->keys[0]; // since left must all be smaller
		new_root->pointers[0].node_ptr = n;
		new_root->pointers[1].node_ptr = sibling;
		
		new_root->is_leaf = false;
		new_root->num_keys = 1;

		*root = new_root;
		
	} else {
		printf("Inserting median(%d) + sibling into parent\n", n->keys[n->num_keys/2] );
	}
	
}

/*
	Insert a record into the B+ Tree.
	If a key already exists we ODKU (On Duplicate Key Update)
	Since this is used as an index so there is no use case for dupe keys.
	We occasionally update and never delete so this seems more useful.
*/
void bpt_insert_or_update( bpt** tree, record r ) {
	
	bpt* root = (*tree);
//	printf("Insert %d:%d\n", r.key, r.value.value_int );

	if( root->is_leaf ) {

		// now we can't just go ahead and add it at the end, we need to keep
		// this shit sorted. (and remember to move the pointers as well)
		// So maybe instead of a keys and pointers array just have a
		// struct with key/pointer. But anyway.
		int k=0;
		while( k<root->num_keys && root->keys[k] < r.key ) { // TODO: this is dumb and should binsearch
			k++;
		}
//		printf("Insertion location: keys[%d] = %d (atend = %d)\n", k, root->keys[k], k == root->num_keys );
		// if we're not appending but inserting, ODKU (we can't check on value since we might be at the end
		// and the value there could be anything)
		if( k < root->num_keys && root->keys[k] == r.key ) {
//			printf("Overwrite of keys[%d] = %d (value %d to %d)\n", k, r.key, root->pointers[k].value_int, r.value.value_int );
			root->pointers[k] = r.value;
			return;
		}
		
		// now insert at k, but first shift everything after k right
		memmove( &root->keys[k+1], &root->keys[k], sizeof(int)*(root->num_keys - k) );
		memmove( &root->pointers[k+1], &root->pointers[k], sizeof(pointer)*(root->num_keys - k) );

		root->keys[k]  = r.key;
		root->pointers[k] = r.value;

		root->num_keys++;

		// split if full
		if( root->num_keys == ORDER ) {
			bpt_split( tree );
		}
		
		return;
	}


	for( int i=0; i<root->num_keys; i++ ) {
//		printf("Checking %d against key %d\n", r.key, root->keys[i] );
		if( r.key < root->keys[i] ) {
//			printf("Must be in left pointer of keys[%d] = %d\n", i, root->keys[i] );
			bpt_insert_or_update( &(root->pointers[i].node_ptr), r );
			return;
		}
	}

//	printf("Wasn't in any left pointers, must be in right then\n");
	if( r.key >= root->keys[root->num_keys-1] ) {
		bpt_insert_or_update( &(root->pointers[root->num_keys].node_ptr), r );
		return;
	}

	fprintf( stderr, "BIG FAT FAIL: insert()\n");
}



node* bpt_find_node( bpt* root, int key ) {
	
	node* current = root;
	int found = false;
	while( !current->is_leaf ) {
		found = false;
		
		for( int i=0; i<current->num_keys; i++ ) {
//			printf("Checking %d against key %d\n", key, current->keys[i] );
			if( key < current->keys[i] ) {
				current = current->pointers[i].node_ptr;
				found = true;
				break;
			}
		}
		if( !found ) {
			current = current->pointers[ current->num_keys ].node_ptr;
		}
	}
	
	return current;
}

record* bpt_find( bpt* root, int key ) {
	
	node* dest_node = bpt_find_node( root, key );
//	printf("Found correct node:\n");
//	print_bpt( dest_node, 0 );
	
	if( dest_node == NULL ) {
		fprintf( stderr, "No node found at all WTF\n");
		exit( EXIT_FAILURE );
	}
	
	// now we have a leaf that *could* contain our key, but it's sorted
	int key_index = binary_search( dest_node->keys, dest_node->num_keys, key );
//	printf("Index for key we're looking for: %d\n", key_index);
	if( key_index < 0 ) {
		return NULL;
	}
	
	record* r = (record*)malloc( sizeof(record) );
	if( r == NULL ) {
		perror("malloc()");
		exit( EXIT_FAILURE );
	}
	r->key = key;
	r->value = dest_node->pointers[key_index]; 
	
	return r;
	
}

void print_bpt_leaf( node* n, int indent ) {
	
	char* ind = malloc( indent+1 );
	for(int i=0; i<indent; i++) { ind[i] = '\t'; }
	ind[indent] = '\0';
	
	printf("%sL-[ ", ind);
	for( int i=0; i<n->num_keys; i++ ) {
		printf("%d ", n->keys[i] );
	}
	printf("]\n");
	free(ind);
}

void print_bpt( bpt* root, int indent ) {

	char* ind = malloc( indent+1 );
	for(int i=0; i<indent; i++) { ind[i] = '\t'; }
	ind[indent] = '\0';

	
	if( root->is_leaf ) {
		print_bpt_leaf( root, indent-1 );
		return;
	}
	printf("%sN %p keys: %d\n", ind, root, root->num_keys);
		
	for( int i=0; i<root->num_keys; i++ ) {
		print_bpt( root->pointers[i].node_ptr, indent+1 );
		printf("%sK-[ %d ]\n", ind, root->keys[i] );
	}
	print_bpt( root->pointers[root->num_keys].node_ptr, indent+1 );

	free(ind);
	
}

unsigned long bpt_count_records( bpt* root ) {
	
	unsigned long count = 0;
	if( root->is_leaf ) {
		return count + root->num_keys;
	}
	
	for( int i=0; i<=root->num_keys; i++ ) { // 1 more pointer than keys
		count += bpt_count_records( root->pointers[i].node_ptr );
	}
	
	return count;
}
