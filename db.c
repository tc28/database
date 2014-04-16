#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#include "db.h"

typedef struct DBNode DBNode;
/* Struct representing a database node */
struct DBNode {
	char *name;
	char *value;
	DBNode *lchild;
	DBNode *rchild;
	pthread_rwlock_t *lock;
};

/* Struct representing a database */
struct Database {
	DBNode *root;
	//database lock removed
	//pthread_rwlock_t *lock;
};

// Forward declarations
static DBNode *dbnode_new(char *name, char *value, DBNode *left, DBNode *right);
static void dbnode_delete(DBNode *node);
static void dbnode_rdelete(DBNode *node);
static DBNode *search(DBNode *parent, char *name, DBNode **parentpp, int is_wrlock);
static void dbnode_print(DBNode *node, FILE *file);
static void dbnode_rprint(DBNode *node, FILE *file, int level);
static void print_spaces(FILE *file, int nspaces);

/****************************************
 * Database creation/deletion functions
 ****************************************/

/* Create a database */
Database *db_new() {
	Database *db = (Database *)malloc(sizeof(Database));
	if(!db)
		return NULL;

	if(!(db->root = dbnode_new("", "", NULL, NULL))) {
		free(db);
		return NULL;
	}
	
	//why do i need to init here?
	//db -> lock = (pthread_rwlock_t *) malloc(sizeof(pthread_rwlock_t));
	//pthread_rwlock_init(db-> lock, 0);
	
	//assigning memory to the lock and initializing the root lock
	(db->root)->lock = (pthread_rwlock_t *) malloc(sizeof(pthread_rwlock_t));
	pthread_rwlock_init((db->root)->lock, 0);
	
	return db;
}

/* Delete a database */
void db_delete(Database *db) {
	
	//do need a lock here
	//pthread_rwlock_wrlock(db -> lock);
	if(db) {
		dbnode_rdelete(db->root);  // Delete all nodes in the database
		free(db);
	}
	//pthread_rwlock_unlock(db -> lock);

}


/* Delete a database node */
static void dbnode_delete(DBNode *node) {
	free(node->name);
	free(node->value);
	free(node);
}

/* Recursively delete a database node and all children */
static void dbnode_rdelete(DBNode *node) {
	if(node) {
		
		//adding lock
		pthread_rwlock_trywrlock(node -> lock);
		
		dbnode_rdelete(node->lchild);
		dbnode_rdelete(node->rchild);
		
		//adding lock
		pthread_rwlock_unlock(node -> lock);
		
		dbnode_delete(node);
	}
}


//no lock needed
/* Create a database node */
static DBNode *dbnode_new(char *name, char *value, DBNode *left, DBNode *right) {
	DBNode *node = (DBNode *)malloc(sizeof(DBNode));
	if(!node)
		return NULL;

	if(!(node->name = (char *)malloc(strlen(name)+1))) {
		free(node);
		return NULL;
	}

	if(!(node->value = (char *)malloc(strlen(value)+1))) {
		free(node->name);	
		free(node);
		return NULL;
	}
	
	node->lock = (pthread_rwlock_t *) malloc(sizeof(pthread_rwlock_t));

	strcpy(node->name, name);
	strcpy(node->value, value);

	node->lchild = left;
	node->rchild = right;
	//initializing lock for
	//
	pthread_rwlock_init(node->lock, 0);

	return node;
}


/****************************************
 * Access/modification functions
 ****************************************/

//apply lock
/* Add a key-value pair to the database
 *
 * db    - The database
 * name  - The key to add
 * value - The corresponding value
 *
 * Return 1 on success, 0 on failure
 */
int db_add(Database *db, char *name, char *value) {
	DBNode *parent;
	  
	//printf("adding name: %s\n", name);
	//first of all
	//lock the root
	pthread_rwlock_rdlock((db->root)->lock);
	DBNode *target = search(db->root, name, &parent, 1);
	//parent locked
	//target, if found, locked
	
	if(target)  {
		// Name is already in database
		//the target is found
		pthread_rwlock_unlock(parent->lock);
		pthread_rwlock_unlock(target->lock);
		return 0;
	}
	
	//if target not found
	target = dbnode_new(name, value, NULL, NULL);

	if(strcmp(name, parent->name)<0)
		parent->lchild = target;
	else
		parent->rchild = target;
	
	//after adding a new node
	//unlock the parent
	pthread_rwlock_unlock(parent->lock);

	return 1;
}

/* Search for the value corresponding to a given key
 *
 * db    - The database
 * name  - The key to search for
 * value - A buffer in which to store the result
 * len   - The result buffer length
 *
 * Return 1 on success, 0 on failure
 */
int db_query(Database *db, char *name, char *value, int len) {
	
	DBNode *parent;
	
	pthread_rwlock_rdlock((db-> root)->lock);
	DBNode *target = search(db->root, name, &parent, 0);
	//target, if exists, locked
	//parent locked

	if(target) {
		int tlen = strlen(target->value) + 1;
		strncpy(value, target->value, (len < tlen ? len : tlen));
		
		pthread_rwlock_unlock(parent->lock);
		pthread_rwlock_unlock(target->lock);
		
		return 1;
	} else {
	  
		pthread_rwlock_unlock(parent->lock);
		return 0;
	}
}

/* Delete a key-value pair from the database
 *
 * db    - The database
 * name  - The key to delete
 *
 * Return 1 on success, 0 on failure
 */
int db_remove(Database *db, char *name) {
	DBNode *parent;
	
	//printf("removing %s\n", name);
	
	pthread_rwlock_wrlock((db->root)->lock);
	DBNode *target = search(db->root, name, &parent, 1);
	//return with the parent locked
	//and the target, if any, locked
	
	
	if(!target)  {
		pthread_rwlock_unlock(parent->lock);
		return 0;
	}
	
	
	DBNode *tleft = target->lchild;
	DBNode *tright = target->rchild;
	
	//I don't know if it is right:
	//releasing the lock on target??
	//maybe no need to release the lock
	//because it will be unlocked in delete
	//locking tleft and tright
	
	//pthread_rwlock_wrlock(tleft->lock);
	//pthread_rwlock_wrlock(tright->lock);
	
	pthread_rwlock_unlock(target->lock);
	dbnode_delete(target);
	
	//parent still locked
	DBNode *successor;
	

	if(!tleft) {
		// If deleted node has no left child, promote right child
		successor = tright;
		//pthread_rwlock_wrlock(successor->lock);
		
	} else if(!tright) {
		// If deleted node has not right child, promote left child
		successor = tleft;
		//pthread_rwlock_wrlock(successor->lock);
	} else {
	  
		// If deleted node has both children, find leftmost child
		// of right subtree.  This node is less than all other nodes in
		// the right subtree, and greater than all nodes in the left subtree,
		// so it can be used to replace the deleted node.
		
		DBNode *sp = NULL;
		successor = tright;
		pthread_rwlock_wrlock(successor->lock);
		
		while(successor->lchild) {
			sp = successor;
			successor = successor->lchild;
			pthread_rwlock_wrlock(successor->lock);
			pthread_rwlock_unlock(sp->lock);
		}

		if(sp) {
			sp->lchild = successor->rchild;
			successor->rchild = tright;
		}
		
		//original 
		successor->lchild = tleft;
		pthread_rwlock_unlock(successor->lock);
		
	}
	
	//
	//pthread_rwlock_unlock(tleft->lock);
	//pthread_rwlock_unlock(tright->lock);
	

	if(strcmp(name, parent->name)<0) {
		parent->lchild = successor;
	} else {
		parent->rchild = successor;
	}
	
	//pthread_rwlock_unlock(successor->lock);
	pthread_rwlock_unlock(parent->lock);
	return 1;
}

//maybe no need to lock the entire database here: 
/* Search the tree, starting at parent, for a node whose name is
 * as specified.
 *
 * Return a pointer to the node if found, or NULL otherwise.
 *
 * If parentpp is not NULL, then it points to a location at which
 * the address of the parent of the target node is stored. 
 * If the target node is not found, the location pointed to by
 * parentpp is set to what would be the the address of the parent
 * of the target node, if it existed.
 *
 * Assumptions: parent is not null and does not contain name
 */


//starting from search
//adding a new arg:
//is_wrlock of type int
//1 if we want to apply write lock
//0 if we want to apply read lock
static DBNode *search(DBNode *parent, char *name, DBNode **parentpp, int is_wrlock) {
	//parent already locked
	DBNode *next = parent;
	//printf("searching for %s\n", name);
	
	do {    
		parent = next;
		
		if(strcmp(name, parent->name) < 0)
			next = parent->lchild;		    
		else	
			next = parent->rchild;
		
		if (next != 0) {
			if (is_wrlock == 1) {
				pthread_rwlock_wrlock(next->lock);
				//deadlock found here
				
			} else {
				pthread_rwlock_rdlock(next->lock);
			}
			
			if (strcmp(name, next->name) == 0) {
				break;
			}			
		}
		
		if (next == 0) break;
		pthread_rwlock_unlock(parent->lock);
		
	} while(1);

	
	
	if(parentpp)
		*parentpp = parent;
	
	//return the parent locked
	//return with the next locked	
	return next;
}

/*********************************************
 * Database printing functions
 *********************************************/

//apply read locks
/* Print contents of database to the given file */
void db_print(Database *db, FILE *file) {
	//pthread_rwlock_rdlock(db->lock);
	dbnode_rprint(db->root, file, 0);
	//pthread_rwlock_unlock(db->lock);
}

//no need to apply locks
/* Print a representation of the given node */
static void dbnode_print(DBNode *node, FILE *file) {
	if(!node)
		fprintf(file, "(null)\n");
	else if(!*(node->name))  // Root node has name ""
		fprintf(file, "(root)\n");
	else
		fprintf(file, "%s %s\n", node->name, node->value);
}

//no need to apply locks
/* Recursively print the given node followed by its left and right subtrees */
static void dbnode_rprint(DBNode *node, FILE *file, int level) {
	print_spaces(file, level);
	dbnode_print(node, file);
	if(node) {
		pthread_rwlock_rdlock(node->lock);
		dbnode_rprint(node->lchild, file, level+1);
		dbnode_rprint(node->rchild, file, level+1);
		pthread_rwlock_unlock(node->lock);
	}
}

//no need to apply locks
/* Print the given number of spaces */
static void print_spaces(FILE *file, int nspaces) {
	while(0 < nspaces--)
		fprintf(file, " ");
}
