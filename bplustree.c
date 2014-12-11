#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "base.h"
#include "utils.h"
#include "c4types.h"
#include "board63.h"
#include "board.h"
#include "bplustree.h"

global_variable struct bpt_counters counters;

internal void database_store_row( database* db, size_t row_index, board* b );
internal void database_store_node( database* db, node* data );
internal void read_database_header( database* db );
internal void write_database_header( database* db );
internal off_t file_offset_from_node( size_t id );
internal off_t file_offset_from_row( size_t row_index );
internal void database_set_filenames( database* db, const char* name );
internal void append_log( database* db, const char* format, ... );
internal void check_tree_correctness( database* db, node* n );
internal key_t max_key( database* db, node* n );

key_t max_key( database* db, node* n ) {
	
	if( n->is_leaf ) {
		return n->keys[ n->num_keys-1 ];
	}
	
	node* last_node = load_node_from_file( db, n->pointers[ n->num_keys ].child_node_id );
	key_t out = max_key( db, last_node );
	free_node( last_node );

	return out;
}

void check_tree_correctness( database* db, node* n ) {
	
	print("Checking correctness of node %lu", n->id );
	
	// for leaves, check if all keys are ascending
	if( n->is_leaf ) {
		
		for(size_t i=0; i<n->num_keys-1; i++ ) {
			assert( n->keys[i] < n->keys[i+1]);
		}
		return;
	}
	

	for(size_t i=0; i<n->num_keys; i++ ) {
		// for every key, the max
		node* temp = load_node_from_file( db, n->pointers[i].child_node_id ); 
		key_t max = max_key( db, temp );
		free_node( temp );
		print("key[%lu] = 0x%lx, max from left node = 0x%lx", i, n->keys[i], max );
		assert( max < n->keys[i] );
	}
	
	// check the final one
	node* temp = load_node_from_file( db, n->pointers[ n->num_keys ].child_node_id ); 
	key_t max = max_key( db, temp );
	free_node( temp );
	print("key[%lu] = 0x%lx, max from right node = 0x%lx", n->num_keys-1, n->keys[ n->num_keys-1], max );
	assert( n->keys[ n->num_keys-1] <= max );
	
}

// the initial node is 1 (0 is reserved)
off_t file_offset_from_node( size_t id ) {
	
	assert( id != 0 );
	off_t offset = sizeof(database_header);
	offset += ((id-1) * sizeof(node));
	return offset;
	
}

off_t file_offset_from_row( size_t row_index ) {
	
	return (off_t)row_index * (off_t)BOARD_SERIALIZATION_NUM_BYTES;
}

void append_log( database* db, const char* format, ... ) {


	char buf[256];
	sprintf( buf, "%s.log", db->name );
	FILE* log = fopen( buf, "a" );

	va_list args;
	va_start( args, format );
	vfprintf( log, format, args );
	va_end( args );
	
	fclose( log );
	
}


void database_store_row( database* db, size_t row_index, board* b ) {

	print("storing board 0x%lx on disk as row %lu", encode_board(b), row_index );

	/* I'm always confused:
		‘r+’ Open an existing file for both reading and writing. The initial contents
		of the file are unchanged and the initial file position is at the beginning
		of the file.
	
		Currently we always append, in later stages we will probably end up deleting rows,
		so we can't use 'a'.
	*/
	off_t offset = file_offset_from_row( row_index );

	// move the file cursor to the initial byte of the row
	FILE* out = open_and_seek( db->table_filename, "r+", offset );

	append_log( db, "storing %lu bytes at offset %llu\n", BOARD_SERIALIZATION_NUM_BYTES, offset );
	
	write_board_record( b, out );
	
	fclose( out );
	
}



void database_store_node( database* db, node* n ) {

	append_log( db, "database_store_node(): writing node %lu (parent %lu) to %s\n", n->id, n->parent_node_id, db->index_filename );
	
	off_t offset = file_offset_from_node( n->id );
	size_t node_block_bytes = sizeof( node );

	// TODO: just fseek since we keep the files open
	FILE* out = open_and_seek( db->index_filename, "r+", offset );
	
	append_log( db, "database_store_node(): storing %lu bytes at offset %llu\n", node_block_bytes, offset );
	
	size_t written = fwrite( n, node_block_bytes, 1, out );
	if( written != 1 ) {
		perror("frwite()");
		exit( EXIT_FAILURE );
	}
	
	fclose( out );

}

// stuff that deals with the fact we store things on disk

void write_database_header( database* db ) {

	append_log( db, "write_database_header(): nodes: %lu - rows: %lu - root node id: %lu\n", db->header->node_count, db->header->table_row_count, db->header->root_node_id );
	fseek( db->index_file, 0, SEEK_SET );
	size_t objects_written = fwrite( db->header, sizeof(database_header), 1, db->index_file );
	if( objects_written != 1 ) {
		perror("frwite()");
		exit( EXIT_FAILURE );
	}
	
}

void read_database_header( database* db ) {
	
	database_header* header = (database_header*) malloc( sizeof(database_header) );
	size_t objects_read = fread( header, sizeof(database_header), 1, db->index_file );
	if( objects_read != 1 ) {
		perror("fread()");
		exit( EXIT_FAILURE );
	}
	
	db->header = header;
	
}

void database_set_filenames( database* db, const char* name ) {

	int written = snprintf( db->index_filename, DATABASE_FILENAME_SIZE, "%s%s", name, ".c4_index" );
	assert( written < DATABASE_FILENAME_SIZE );

	written = snprintf( db->table_filename, DATABASE_FILENAME_SIZE, "%s%s", name, ".c4_table" );
	assert( written < DATABASE_FILENAME_SIZE );
	
}


database* database_create( const char* name ) {


	database* db = (database*) malloc( sizeof(database) );
	if( db == NULL ) {
		perror("malloc()");
		exit( EXIT_FAILURE );
	}
	
	db->name = name;
	
	db->header = (database_header*) malloc( sizeof(database_header) );
	
	db->header->table_row_count = 0;
	db->header->node_count = 0;

	// create a new bpt
	node* first_node = new_bptree( ++db->header->node_count ); // get the next node id, and update count
	db->header->root_node_id = first_node->id;
	
	database_set_filenames( db, name );

	// create the index file
	create_empty_file( db->index_filename );
	db->index_file = fopen( db->index_filename, "r+" );
	print("created index file %s", db->index_filename);
	
	// create the table file
	create_empty_file( db->table_filename );
	db->table_file = fopen( db->table_filename, "r+" );
	
	print("created table file %s", db->table_filename);
	
	write_database_header( db );

	// it's empty, but just in case you call create/close or something	
	database_store_node( db, first_node );
	free_node( first_node );
	

	return db;
}

database* database_open( const char* name ) {

	
	database* db = (database*) malloc( sizeof(database) );
	if( db == NULL ) {
		perror("malloc()");
		exit( EXIT_FAILURE );
	}
	
	database_set_filenames( db, name );

	// open the index file
	db->index_file = fopen( db->index_filename, "r+" );
	print("opened index file %s", db->index_filename);
	
	// open the table file
	db->table_file = fopen( db->table_filename, "r+" );
	print("opened table file %s", db->table_filename);
	
	// read the root node, node_count and row_count
	read_database_header( db );
	print("nodes: %lu, rows: %lu, root node ID: %lu", db->header->node_count, db->header->table_row_count, db->header->root_node_id );

	
	return db;	
}

void database_close( database* db ) {
	
	print("closing %s and %s", db->table_filename, db->index_filename);
	
	write_database_header( db );

	free( db->header );
	
	fclose( db->index_file );
	fclose( db->table_file );

	free( db );
	
	prints("closed");
}

global_variable key_t stored_keys[1024];
global_variable int num_stored_keys;

bool database_put( database* db, board* b ) {
	
	// the index knows if we already have this record,
	// but in order to store it we need to have the offset in the table
	// which we know as we can just keep track of that
	
	board63 board_key = encode_board( b );
	print("key for this board: 0x%lx", board_key );
	
	record r = { .key = board_key, .value.table_row_index = db->header->table_row_count };

	// TODO(bug): this should not overwrite, that means wasting space in the table file, also return inserted/dupe
	counters.key_inserts++;

	print("loading root node ID %lu", db->header->root_node_id );
	node* root_node = load_node_from_file( db, db->header->root_node_id );
	bpt_print( db, root_node , 0 );

	bool inserted = bpt_insert_or_update( db, root_node, r );
	print("inserted: %s", inserted ? "true" : "false");
	free_node( root_node );
	
	// BUG HERE
	// tree might have grown, and since it grows upward *root might not point at the
	// actual root anymore. But since all parent pointers are set we can traverse up
	// to find the actual root
	prints("finding possible new root after insert");
	root_node = load_node_from_file( db, db->header->root_node_id );
	print("Current root node %lu (parent %lu)", root_node->id, root_node->parent_node_id );
	while( root_node->parent_node_id != 0 ) {
		print("%lu is not the root, moving up to node %lu", root_node->id, root_node->parent_node_id );
		node* up = load_node_from_file( db, root_node->parent_node_id );
		free_node( root_node );
		root_node = up;
	}
	db->header->root_node_id = root_node->id;
	
	print( "after insert: root node id: %lu (%p)", db->header->root_node_id, root_node );
	check_tree_correctness (db, root_node );
	free_node( root_node );
	
	// now write the data as a "row" to the table file
	if( inserted ) {
		key_t latest_key = encode_board( b );
		print("Latest key: 0x%lx", latest_key);
		for(int i=0; i< num_stored_keys; i++) {
			print("Stored[%02d]: 0x%lx", i, stored_keys[i]);
			assert( stored_keys[i] != latest_key );
		}
		stored_keys[ num_stored_keys++ ] = latest_key;
		database_store_row( db, db->header->table_row_count, b );
		db->header->table_row_count++;
	}
	
	return inserted;
	
}

/*
	Search keys for a target_key.
	returns
		BINSEARCH_FOUND: target_key is an element of keys, key_index is set to the location
		BINSEARCH_INSERT: target_key is NOT an element of keys, key_index is set to the index where you would need to insert target_key
		BINSEARCH_ERROR: keys is NULL, key_index is unchanged

*/
#define BINSEARCH_ERROR 0
#define BINSEARCH_FOUND 1
#define BINSEARCH_INSERT 2

internal unsigned char binary_search( key_t* keys, size_t num_keys, key_t target_key, size_t* key_index ) {

	// no data at all
	if( keys == NULL ) {
		return BINSEARCH_ERROR;
	}
	
	// empty array, or insert location should be initial element
	if( num_keys == 0 || target_key < keys[0] ) {
		*key_index = 0;
		return BINSEARCH_INSERT;
	}
	
	size_t span = num_keys;
	size_t mid = num_keys / 2;
	size_t large_half;
	while( span > 0 ) {

		counters.key_compares++;
		if( target_key == keys[mid] ) {
			*key_index = mid;
			return BINSEARCH_FOUND;
		}
		
		span = span/2; // half the range left over
		large_half = span/2 + (span % 2);// being clever. But this is ceil 

		counters.key_compares++;
		if( target_key < keys[mid] ) {
			mid -= large_half;
		} else {
			mid += large_half;
		}
		
	}

	// target_key is not an element of keys, but we found the closest location
	counters.key_compares++;
	if( mid == num_keys ) { // after all other elements
		*key_index = num_keys;
	} else if( target_key < keys[mid] ) {
		*key_index = mid; // displace, shift the rest right
	} else if( target_key > keys[mid] ) {
		*key_index = mid+1; // not sure if these two are both possible
	} else {
		assert(0); // cannot happen
	}


	// correctness checks:
	// 1. array has elements, and we should insert at the end, make sure the last element is smaller than the new one
	if( num_keys > 0 && *key_index == num_keys ) {
		assert( target_key > keys[num_keys-1] );
	}
	// 2. array has no elements (we already check this above, but left for completeness)
	if( num_keys == 0 ) {
		assert( *key_index == 0 );
	}
	// 3. array has elements, and we should insert at the beginning
	if( num_keys > 0 && *key_index == 0 ) {
		assert( target_key < keys[0] ); // MUST be smaller, otherwise it would have been found if equal
	}
	// 4. insert somewhere in the middle
	if( *key_index > 0 && *key_index < num_keys ) {
		assert( target_key < keys[*key_index] ); // insert shifts the rest right, MUST be smaller otherwise it would have been found
		assert( keys[*key_index-1] < target_key ); // element to the left is smaller
	}

	return BINSEARCH_INSERT;
}

void free_node( node* n ) {

	counters.frees++;
//	print("ID %lu (%p) (cr: %llu/ld: %llu/fr: %llu)", n->id, n, counters.creates, counters.loads, counters.frees );

	assert( n != NULL );
	assert( n->id != 0 );

	assert( counters.frees <= counters.loads + counters.creates );

	n->id = 0;
	free( n );

	
}

node* new_bptree( size_t node_id ) {
	
	node* out = (node*)malloc( sizeof(node) );
	if( out == NULL ) {
		perror("malloc()");
		exit( EXIT_FAILURE );
	}
	
	out->id = node_id;
	
	out->parent_node_id = 0;
	
	out->num_keys = 0;
	out->is_leaf = true;
	
	counters.creates++;
	
	print("created node ID: %lu (%p) (cr: %llu/ld: %llu/fr: %llu)", out->id, out, counters.creates, counters.loads, counters.frees  );
	return out;
}

void bpt_dump_cf() {
	printf("BPT ORDER: %d\n", ORDER);
	printf("Total creates: %llu\n", counters.creates);
	printf("Total loads: %llu\n", counters.loads);
	printf("Total frees: %llu\n", counters.frees);
	printf("Total key inserts: %llu\n", counters.key_inserts);
	printf("Total get calls: %llu\n", counters.get_calls);
	printf("Total splits: %llu\n", counters.splits);
	printf("Total insert calls: %llu\n", counters.insert_calls);
	printf("Total parent_inserts: %llu\n", counters.parent_inserts);
	printf("Total generic key compares: %llu\n", counters.key_compares);
	printf("Total leaf key compares: %llu\n", counters.leaf_key_compares);
	printf("Total node key compares: %llu\n", counters.node_key_compares);
	if( counters.key_inserts > 0 ) {
		printf("Key compares (leaf+node) per key insert: %llu\n", counters.key_compares / counters.key_inserts );
	}
	printf("Anycounter: %llu\n", counters.any);
	assert( (counters.creates + counters.loads) == counters.frees );
}

void bpt_insert_node( database* db, node* n, key_t up_key, size_t node_to_insert_id ) {

	assert( n->id != 0 );
	check_tree_correctness( db, n );

	size_t k=0;
	while( k<n->num_keys && n->keys[k] < up_key ) { // TODO(performance): use binsearch
		k++;
	}
	print("insert key 0x%lx at position %zu, node at position %zu", up_key, k, k+1);
	// move keys over (could be 0 if at end)
	size_t elements_moving_right = n->num_keys - k;
//	printf("Moving %zu elements\n", elements_moving_right);
	
	// move keys right and set the key in the open space
	memmove( &n->keys[k+1], &n->keys[k], KEY_SIZE*elements_moving_right);
	n->keys[k] = up_key;
	
	// move pointers over (given that sibling is the right part of the split node,
	// move 1 more than the keys)
	memmove( &n->pointers[k+2], &n->pointers[k+1], sizeof(pointer)*elements_moving_right );
	n->pointers[k+1].child_node_id = node_to_insert_id;

	n->num_keys++;

#ifdef VERBOSE
	prints("after insert:");
	bpt_print( db, n, 0 );
#endif

	check_tree_correctness( db, n );

	// we might need to split (again)
	if( n->num_keys == ORDER ) {
		print("hit limit, have to split node %lu (%p)", n->id, n);
		bpt_split( db, n ); // propagate new node up
		return;
	} else {
		prints("did not split");
		database_store_node( db, n );
	}

}



void bpt_split( database* db, node* n ) {
	counters.splits++;

	print("node %lu: %s (%p)", n->id, (n->is_leaf ? "leaf" : "node"), n );
	bpt_print( db, n, 0 );
	/* 
	Create a sibling node
	
	2 cases: (1) the node is a leaf, (2) the node isn't a leaf
	
	(1) - Leaf node
	
		Imagine a btp with ORDER=4. This means we split when we have 4 keys: [ 1 2 3 4 ]
		We split this into 2 nodes around key index 1 = [ 2 ]
		Result:	
					[ 2 ]
			 [ 1 ]    [ 2 3 4 ]
		This means key 2 is pushed up, and we still have key 2 in the sibling node since that's
		where we store the associated value.
		This means the split means copying 3 keys/values from the node to the sibling, and pushing key 2 up.
	
	(2) - Not a leaf node
	
		Example: [ 2 3 4 5 ]
		But now these hold keys, and the values are pointers to nodes that hold the same keys (and the values).
	
		Initial situation after inserting 7:
				[2]  [3]  [4]		    <-- keys + node pointers
			[1]  [2]  [3]  [4 5 6 7] <-- keys + values
	
		The end structure should look like this:
	
							[3]						<-- keys + node pointers
					[2]			[4] [5]			<-- keys + node pointers
				[1]  [2]    [3]  [4]  [5 6 7]	<-- keys + values
	
		So first we split the leaf node [4 5 6 7] like before, and we end up with:
	
				[2]  [3]  [4]  [5]		   <-- keys + node pointers
			[1]  [2]  [3]  [4]  [5 6 7]   <-- keys + values
	
		Now we need to split the node with keys + pointers, so call bpt_split() again.
	
		We need to push up key 3, but copy 2 keys: [4 5] to the sibling, and 3 node pointers: [3] [4] [5 6 7]
		The reason is that we don't need an intermediary key 3 since the key+value of 3 are already in a 
		leaf node at the bottom.
	
		Intermediate result:
	
			Node				Sibling
			[2]			 [4]  [5]
		[1]  [2]     [3]  [4]  [5 6 7]
	
		Now 2 more cases: (a) node above this level, (b) no node above this level
	
		(b) - No node above this level (it's easier)
	
		Create a new node with 1 key (3) and 2 node pointers to Node and Sibling
		Set the parent pointers of Node and Sibling to point to the new root.
	
		(a) - Node above this level
		
		This node must already have a key and a pointer to Node (and the key in this example must be 1)
		
		Insert key 3 in the parent node (shifting others if needed)
		Insert Sibling in the parent node (shifting others if needed)
		
		
	So in all cases we need to create a sibling node and copy items over:
	
	is_leaf:
			copy K keys: ORDER/2 + 1 (for even ORDER: half+1, for odd ORDER: the larger half)
			copy N values (same as the number of keys)
	
	!is_leaf:
			copy K keys: ORDER - ORDER/2 (for even ORDER: half, for odd ORDER: the larger half)
			copy N pointers: K+1
	*/

	// first step: create sibling and copy the right amount of keys/values/pointers
	// when splitting a !leaf: the middle item goes up (and we don't store that intermediate key)
	// to in case of an ODD keycount: just floor(order/2) (example: 3 -> 1, 9 -> 4)
	// in case of an EVEN keycount: the larger half: floor(order/2) (example: 4 -> 2, 10 -> 5)
	size_t keys_moving_right = n->is_leaf ? ORDER/2 + 1 : ORDER/2;
	size_t offset = n->is_leaf ? SPLIT_KEY_INDEX : SPLIT_NODE_INDEX;
	print("moving %zu keys (%zu nodes/values) right from offset %zu (key = 0x%lx)", keys_moving_right, keys_moving_right+1, offset, n->keys[offset] );

	node* sibling = new_bptree( ++db->header->node_count );	
	memcpy( &sibling->keys[0], &n->keys[offset], KEY_SIZE*keys_moving_right );
	memcpy( &sibling->pointers[0], &n->pointers[offset], sizeof(pointer)*(keys_moving_right+1) );
	
	// housekeeping
	sibling->parent_node_id = n->parent_node_id;
	sibling->num_keys = keys_moving_right;
	sibling->is_leaf = n->is_leaf; // if n was NOT a leaf, then sibling can't be either.
	
	// if the node had subnodes (!is_leaf) then the subnodes copied to the sibling need
	// their parent pointer updated
	// TODO(performance): keep the subnodes around so update parent pointer doesn't hit the disk
	if( !n->is_leaf ) {
		for(size_t i=0; i < sibling->num_keys+1; i++ ) {
			node* s = load_node_from_file( db, sibling->pointers[i].child_node_id );
			s->parent_node_id = sibling->id;
			database_store_node( db, s );
			free_node( s );
		}
	}
	
	
	// when splitting a node a key and 2 nodes mode up
	n->num_keys = n->num_keys - keys_moving_right - (n->is_leaf ? 0 : 1);

#ifdef VERBOSE
	print("created sibling node %lu (%p)", sibling->id, sibling);
	bpt_print( db, sibling, 0 );
	print("node %lu left over (%p)", n->id, n);
	bpt_print( db, n, 0 );
#endif


	database_store_node( db, n );
	database_store_node( db, sibling ); // store it first, in case bpt_insert_node needs to load us
	
	// the key that moves up is the split key in the leaf node case, and otherwise
	// the key we didn't propagate to the sibling node and didn't keep in the node
	// which is the one we're splitting around... so in both cases this is the same.
	key_t up_key = n->keys[SPLIT_KEY_INDEX];
	print("key that moves up: 0x%lx", up_key);
	// now insert median into our parent, along with sibling
	// but if parent is NULL, we're at the root and need to make a new one
	if( n->parent_node_id == 0 ) {

		print("no parent, creating new root: ID %lu", db->header->node_count + 1 );
		node* new_root = new_bptree( ++db->header->node_count );

		new_root->keys[0] = up_key; // since left must all be smaller
		new_root->pointers[0].child_node_id = n->id;
		new_root->pointers[1].child_node_id = sibling->id;

		new_root->is_leaf = false;
		new_root->num_keys = 1;

		n->parent_node_id = sibling->parent_node_id = new_root->id;

		database_store_node( db, new_root );
		free_node( new_root );

		print("n->p %lu s->p %lu", n->parent_node_id, sibling->parent_node_id);
		
		// TODO(performance): we potentially store n+sibling twice in a row here
		database_store_node( db, n );
		database_store_node( db, sibling );

	} else {
		print("inserting key 0x%lx + sibling node %lu into parent %lu", up_key, sibling->id, n->parent_node_id );

		// so what we have here is (Node)Key(Node) so we need to insert this into the
		// parent as a package. Parent also has NkNkNkN and we've just replaced a Node
		// with a NkN. The parent is actually kkk, NNNN so finding where to insert the key
		// is easy and obvious, and the node we can just add the sibling beside our current node
		// (since in essence we represent the same data as before so must be adjacent)
		
		// now this is fairly doable, but it might lead to having to split the parent
		// as well
#ifdef VERBOSE
		print("going to insert into parent node ID %lu", n->parent_node_id);
		node* temp = load_node_from_file( db, n->parent_node_id );
		bpt_print( db, temp, 0 );
		free_node( temp );
#endif		
		counters.parent_inserts++;
		// TODO(performance): node cache
		node* parent = load_node_from_file( db, n->parent_node_id );
		bpt_insert_node( db, parent, up_key, sibling->id );
		free_node( parent );
	}
	check_tree_correctness (db, sibling );
	check_tree_correctness (db, n );

	free_node( sibling );
	
}

/*
	Insert a record into the B+ Tree.
	If a key already exists we ODKU (On Duplicate Key Update)
	Since this is used as an index so there is no use case for dupe keys.
	We occasionally update and never delete so this seems more useful.
	But maybe not Update but Ignore instead
*/
bool bpt_insert_or_update( database* db, node* root, record r ) {

	assert( root->id != 0 );
	
	counters.insert_calls++;
	print("node %lu - %p", root->id, root);

//	printf("Insert %d:%d\n", r.key, r.value.value_int );
	size_t k=0;
	size_t insert_location = 0;

	if( root->is_leaf ) {
		bpt_print( db, root, 0 );

		int bsearch_result = binary_search( root->keys, root->num_keys, r.key, &insert_location);
		assert( bsearch_result == BINSEARCH_FOUND || bsearch_result == BINSEARCH_INSERT );
		print("Leaf node - insert location: %lu", insert_location);
		
		k = insert_location;
//		printf("Insertion location: keys[%d] = %d (atend = %d)\n", k, root->keys[k], k == root->num_keys );
		// if we're not appending but inserting, ODKU (we can't check on value since we might be at the end
		// and the value there could be anything)
		if( k < root->num_keys && root->keys[k] == r.key ) {
			print("Overwrite of keys[%lu] = %lu (value %lu to %lu)", k, r.key, root->pointers[k].table_row_index, r.value.table_row_index );
			root->pointers[k] = r.value;
			
			return false; // was not inserted but updated (or ignored)
		}
		
		// now insert at k, but first shift everything after k right
		// TODO: These don't overlap afaict, why not memcpy? src==dest maybe?
		memmove( &root->keys[k+1], &root->keys[k], KEY_SIZE*(root->num_keys - k) );
		memmove( &root->pointers[k+1], &root->pointers[k], sizeof(pointer)*(root->num_keys - k) );

		root->keys[k]  = r.key;
		root->pointers[k] = r.value;

		root->num_keys++;

		// split if full 
		// TODO(bug?): figure out when to save the root node, or maybe not at all?
		if( root->num_keys == ORDER ) {
			print("hit limit, have to split node %lu (%p)", root->id, root);
			bpt_split( db, root );
		} else {
			database_store_node( db, root );
		}
		
		return true; // an insert happened
	}


	// NOT a leaf node, recurse to insert
	// TODO: maybe just find_node()?
	int binsearch_result = binary_search( root->keys, root->num_keys, r.key, &insert_location);
	assert( binsearch_result == BINSEARCH_FOUND || binsearch_result == BINSEARCH_INSERT );

	if( binsearch_result == BINSEARCH_FOUND ) {
		print("key 0x%lx is already stored, bailing out.", r.key );
		return false; // TODO(clarity): maybe constants for these
	}

	// descend a node
	// TODO(bug?): maybe we don't always pick the right node here?
	if( insert_location < root->num_keys ) {
		print("Must be in node to the left of keys[%lu] = 0x%lx (node %lu)", insert_location, root->keys[insert_location], root->pointers[insert_location].child_node_id );
	} else {
		print("Must be in node to the right of keys[%lu] = 0x%lx (node %lu)", insert_location-1, root->keys[insert_location-1], root->pointers[insert_location].child_node_id );
	}
	print("Descending into node %lu", root->pointers[insert_location].child_node_id);

	// TODO(performance): node cache
	node* target = load_node_from_file( db, root->pointers[insert_location].child_node_id );
	bool was_insert = bpt_insert_or_update( db, target, r );
	print("inserted into child node (was_insert: %s)", ( was_insert ? "true" : "false") );
	free_node( target );
	
	return was_insert;
}

/*
	Return the leaf node that should have key in it.
*/
internal node* bpt_find_node( database* db, node* root, key_t key ) {
	
	assert( root->id != 0 );
	
	node* current = root;
	
	while( !current->is_leaf ) {

		print("checking node %lu", current->id);
		
		size_t node_index;
		switch( binary_search( current->keys, current->num_keys, key, &node_index) ) {
			case BINSEARCH_FOUND:
				node_index++; // if we hit the key, the correct node is to the right of that key
				break; // needless break here
			case BINSEARCH_INSERT:
				// insert means we determined the target is to the left of this key (which is the correct node) 
				break;
			case BINSEARCH_ERROR:
			default:
				fprintf( stderr, "Somehow have a node with a NULL keys array\n");
				assert(0);
		}

		// correctness checks
		if( node_index == 0 ) { // target should be smaller than key 0
			assert( key < current->keys[node_index] );
		} else { // target should be equal/bigger than the key to the left of that node 
			assert( key >= current->keys[node_index-1] );
		}

		size_t next_node_id = current->pointers[node_index].child_node_id;
		print("Moving to child node %lu", next_node_id);
		// TODO(elegance): must be a way to do this more elegant and without the if
		if( current != root ) {
			free_node( current );
		}
		current = load_node_from_file( db, next_node_id );
	}
	
	print("returning node %lu", current->id);
	return current;
}

node* load_node_from_file( database* db, size_t node_id ) {

	size_t node_block_bytes = sizeof( node );
	off_t node_block_offset = file_offset_from_node( node_id );

	// move the file cursor to the initial byte of the row
	// fseek returns nonzero on failure
	if( fseek( db->index_file, (long) node_block_offset, SEEK_SET ) ) {
		perror("fseek()");
		exit( EXIT_FAILURE );
	}
	
	node* n = (node*) malloc( sizeof(node) );
	size_t objects_read = fread( n, node_block_bytes, 1, db->index_file );
	if( objects_read != 1 ) {
		perror("fread()");
		free( n );
		print("unable to load node %lu", node_id);
		return NULL; // couldn't read a node
	}

	counters.loads++;

	append_log( db, "load_node_from_file(): retrieved node %lu (%p) (cr: %llu/ld: %llu/fr: %llu)\n", node_id, n, counters.creates, counters.loads, counters.frees );
	return n;
}

board* load_row_from_file( FILE* in, off_t offset ) {

	if( fseek( in, (long) offset, SEEK_SET ) ) {
		perror("fseek()");
		return NULL;
	}
	
	return read_board_record( in );
}

board* database_get( database* db, key_t key ) {
	
	print("loading root node ID %lu", db->header->root_node_id );
	node* root_node = load_node_from_file( db, db->header->root_node_id );
	record* r = bpt_get( db, root_node, key );
	free_node( root_node );
	
	if( r == NULL ) {
		return NULL;
	}
	
	off_t offset = file_offset_from_row( r->value.table_row_index );
	print("Row offset: %llu", offset);

	return load_row_from_file( db->table_file, offset );
}

record* bpt_get( database* db, node* root, key_t key ) {

	assert( root->id != 0 );
	
	counters.get_calls++;
	print("key 0x%lx", key);
	node* dest_node = bpt_find_node( db, root, key );
	print("Found correct node %lu", dest_node->id);
	//bpt_print( dest_node, 0 );
	
	if( dest_node == NULL ) {
		fprintf( stderr, "No node found at all WTF\n");
		exit( EXIT_FAILURE );
	}
	
	// now we have a leaf that *could* contain our key, but it's sorted
	size_t key_index;
	if( BINSEARCH_FOUND != binary_search( dest_node->keys, dest_node->num_keys, key, &key_index ) ) {
		prints("Key not found");
		if( dest_node->id != root->id ) {
			free_node( dest_node );
		}
		return NULL;
	}
	print("Key index: %lu", key_index);
	
	record* r = (record*)malloc( sizeof(record) );
	if( r == NULL ) {
		perror("malloc()");
		exit( EXIT_FAILURE );
	}
	r->key = key;
	r->value = dest_node->pointers[key_index]; 
	print("returning record { key = 0x%lx, value.table_row_index = %lu / value.child_node_id = %lu }", r->key, r->value.table_row_index, r->value.child_node_id);

	if( dest_node->id != root->id ) {
		print("dest id %lu root id %lu", dest_node->id, root->id);
		free_node( dest_node );
	}
	return r;
	
}

internal void bpt_print_leaf( node* n, int indent ) {
	
	char ind[100] = "                               END";
	ind[indent*2] = '\0';
	
	if( indent == 0 ) {
		printf("+---------------------- LEAF NODE %lu --------------------------+\n", n->id);
	}
	printf("%sL(%lu)-[ ", ind, n->id);
	for( size_t i=0; i<n->num_keys; i++ ) {
		printf("0x%lx ", n->keys[i] );
	}
	printf("] - (%p) (#keys: %lu, parent id %lu)\n", n, n->num_keys, n->parent_node_id);
	if( indent == 0 ) {
		printf("+---------------------- END LEAF NODE %lu --------------------------+\n", n->id);
	}

}

void bpt_print( database* db, node* start, int indent ) {
#ifdef VERBOSE
	char ind[100] = "                               END";
	ind[indent*2] = '\0';
	
	if( start->is_leaf ) {
		bpt_print_leaf( start, indent );
		return;
	}
	if( indent == 0 ) {
		printf("+---------------------- NODE %lu --------------------------+\n", start->id );
	}
	printf("%sN(%lu) (%p) keys: %zu (parent id %lu)\n", ind, start->id, start, start->num_keys, start->parent_node_id);
		
	// print every key/node
	node* n;
	for( size_t i=0; i<start->num_keys; i++ ) {
		n = load_node_from_file( db, start->pointers[i].child_node_id  );
		assert( n != NULL );

		if( n->is_leaf ) {
			bpt_print_leaf( n, indent+1 );
		} else {
			bpt_print( db, n, indent+1 );			
		}
		printf("%sK[%lu]-[ 0x%lx ]\n", ind, i, start->keys[i] );
		free_node( n );
		
	}
	// print the last node
	n = load_node_from_file( db, start->pointers[start->num_keys].child_node_id  ); 
	assert( n != NULL );
	bpt_print( db, n, indent + 1 );
	free_node( n );
	if( indent == 0 ) {
		printf("+---------------------- END NODE %lu --------------------------+\n", start->id);
	}
#endif
}

// NOTE(performance): this could be very slow, but I'm not sure that will ever be relevant
size_t bpt_size( database* db, node* start ) {
	counters.any++;
	
	if( start->is_leaf ) {
		return start->num_keys;
	}
	
	size_t count = 0;
	for( size_t i=0; i<=start->num_keys; i++ ) { // 1 more pointer than keys
		node* child = load_node_from_file( db, start->pointers[i].child_node_id );
		count += bpt_size( db, child );
		free_node( child );		
	}
	
	return count;
}

