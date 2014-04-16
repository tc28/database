#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <pthread.h>

#include "window.h"
#include "io_functions.h"
#include "db.h"



/* Struct encapsulating information about a client */
typedef struct Client {
	Window *window;  // The client window
	pthread_t *my_pthread;
} Client;



// Global variables
Database *db;
char *scriptname;

// Forward declarations
Client *client_new(int id);
void client_delete(Client *client);
void *run_client(void *client);
void process_command(char *command, char *response);

//Tian Chen:
//mutex for thread counter
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
int thread_counter; 

//Tian Chen:
//mutex for s and g
//stop and go functions
int if_stopped; //1 if stopped, 0 if not stopped
pthread_mutex_t cond_mutex;//initialized elsewhere
pthread_cond_t cond;//initialized elsewhere


/*
Tian Chen:
implementation of the linked nodes of pthread pointers
*/
//node definition
//each pthreadnode contains
//a value: a pthread_t pointer
//a pointer to the next pthreadnode
typedef struct PthreadNode {
	//a pointer to a pthread_t pointer
	pthread_t *value;
	struct PthreadNode *next;
} PthreadNode;

//global variables for the linkedlist
//the head of the linkedlist
//0 if the linkedlist does not exist
PthreadNode *head;
//the end of the linkedlist
//0 if the linkedlist does not exist
PthreadNode *end;

//mutex 
pthread_mutex_t node_list_mutex;


//adding the pthread towards the end of the list
//input: a pthread pointer (pthread_t *) to be stored as the value
//of the new node
//no output
void add_pthread(pthread_t *pthread_pointer) {
  
	pthread_mutex_lock(&node_list_mutex);
	PthreadNode *new_node = (PthreadNode *)malloc(sizeof(PthreadNode));
 	//new_node -> value = (pthread_t *)malloc(sizeof(pthread_t *));
 	//new_node -> next = (PthreadNode *)malloc(sizeof(PthreadNode *));
	new_node -> value = pthread_pointer;
	
	//an empty list case
	if (head == 0) {
		head = new_node;
		end = new_node;
		new_node -> next = 0;
	} else {
	//if not empty
	//add at the end of the list
		end -> next = new_node;
		new_node -> next = 0;
		
		end = new_node;
	}  
	pthread_mutex_unlock(&node_list_mutex);
}



//remove
//first search for the pthread_pointer in the linkedlist
//and then remove this
//input: a pthread pointer (pthread_t *), which needs to be found and removed
//returns 0 if it's removed
//returns 1 if it's not found
int remove_pthread (pthread_t *pthread_pointer) {
	
	pthread_mutex_lock(&node_list_mutex);
	//meaning that there is only one node
	
	if (head == end) {
		if ((head -> value) == pthread_pointer) {
		//the only thread is what we want
			head = 0;
			end = 0;
			pthread_mutex_unlock(&node_list_mutex);
			return 0;
		} else {
			pthread_mutex_unlock(&node_list_mutex);
			return 1;
		}
	} else {
		
		PthreadNode *current_node = head;
		PthreadNode *previous_node;
		
		//question:
		//still need to confirm why adding "free" does not work
		while (current_node != 0) {
			if ((current_node -> value) == pthread_pointer) {
				if (current_node == head) {

					head = current_node->next;
					//free(current_node -> value);
					free(current_node);
					pthread_mutex_unlock(&node_list_mutex);
					return 0;
					
				} else if (current_node == end) {
					end = previous_node;
					end->next = 0;
					
					//free(current_node -> value);
					free(current_node);
					pthread_mutex_unlock(&node_list_mutex);
					return 0;
					
				} else {
					//no need to change 
					//the head or the end	
					previous_node -> next = current_node -> next;
					
					//why should't we have these things?
					//free(current_node->value);
					//free(current_node->next);
					free(current_node);
					pthread_mutex_unlock(&node_list_mutex);
					return 0;
				}			   
			}
			previous_node = current_node;		
			current_node = current_node -> next;
		}
		
		pthread_mutex_unlock(&node_list_mutex);
		return 1;
	} 
}

//a method for debugging purpose
//printing the linkedlist 
//currently not used in my implementation
void print_pthreads() {
  
	pthread_mutex_lock(&node_list_mutex);
	if (head == 0) {
		printf("(nil)\n");
		pthread_mutex_unlock(&node_list_mutex);
		return;  
	}
	PthreadNode *current_node = head;
	
	printf("\n");
	printf("start\n");
	while (current_node != 0) {
		printf("current node: %p\n", (void *) current_node);
		printf("value: %p\n", (void *)(current_node->value));
		printf("next pointer: %p\n", (void *)(current_node->next));
		current_node = current_node -> next;
		printf("-----------\n");
	}  
	printf("end\n");
	printf("\n");
	pthread_mutex_unlock(&node_list_mutex);
}
/*********************************************/

//Tian Chen:
//for CTRL+C purpose
//clean_up() waits for a SIGINT
//and cancels all the pthreads 
//currently stored in the linkedlist
void *clean_up(void * input) {
	//first of all
	//sigwait
	int return_sig;
	sigset_t my_set;
	sigemptyset(&my_set);
	sigaddset(&my_set, SIGINT);
	while(1) {
		sigwait(&my_set, &return_sig);
		
		PthreadNode *current_node = head;
		while (current_node != 0) {		
			if (pthread_cancel(*(current_node -> value))) {
				perror("FATAL: cancellation failed\n");
				exit(1);
			}			
			current_node = current_node -> next;
		}
		
	}
	input = NULL;
	return NULL;
}

//Tian Chen:
//for CTRL+D purpose
//no need to wait for any signals
//cancel all running clients immediately

//condition variables 
//granting the main thread to proceed to cancel itself
//after all the clients are deleted for sure
int all_cleanedup;
pthread_mutex_t cleanup_cond_mutex;//initialized in main
pthread_cond_t cleanup_cond;//initialized in main

//clean_up_fast: cancels all the clients in the list immediately
//without waiting for signals
//input: trival
//output: trival
void *clean_up_fast(void *input) {
	PthreadNode *current_node = head;
	while (current_node != 0) {
		if (pthread_cancel(*(current_node -> value))) {
			perror("FATAL: pthread deletion failed\n");
			exit(1);
		}
		current_node = current_node -> next;	
	}
	
	input = NULL;
	return NULL;  
}

//copied from the assignment
//wrapper for cleanup handler
void cleanup_pthread_mutex_unlock(void *arg) {
	pthread_mutex_unlock((pthread_mutex_t *) arg);
}


//Tian Chen:
//a wrapper function to be pushed 
//to the cleanup handler
//which warps the function of client_delete
//input: arg of type void*, which is inherently a Client *
void cleanup_pthread_client_delete(void *arg) {

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	client_delete((Client *) arg);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	//if no thread counter exists
	//set all_cleaned up to one
	//this could happen in a Ctrl+D situation
	//but if it does not happen in a Ctrl+D situation
	//it's still Okay because 
	//all_cleanedup will still be reset to 0
	if (thread_counter == 0) {
		pthread_mutex_lock(&cleanup_cond_mutex);
		all_cleanedup = 1;
		pthread_cond_broadcast(&cleanup_cond);
		pthread_mutex_unlock(&cleanup_cond_mutex);
	}
}

//the second to wait for each client
int wait_s;


//a wrapper that I have implemented
//
void pthread_deletion_wrapper(void *pthread) {
	pthread_cancel(*((pthread_t *) pthread));  
}
/***********************************
 * Main function
 ***********************************/

int main(int argc, char **argv) {
	
	//as a counter of threads
	thread_counter = 0;	
	
	//initializing condition variable
	//for cleanup purposes
	pthread_cond_init(&cleanup_cond, 0);
	pthread_mutex_init(&cleanup_cond_mutex, 0);
	
	//initializing condition variable
	//for stop and go function
	pthread_cond_init(&cond, 0);
	pthread_mutex_init(&cond_mutex, 0);
	
	//initializing the lock on the
	//linkedlist for pthreads
	pthread_mutex_init(&node_list_mutex, 0);	
	
	if(argc == 1) {
		scriptname = NULL;
	} else if(argc == 2) {
		int len = strlen(argv[1]);
		scriptname = malloc(len+1);
		strncpy(scriptname, argv[1], len+1);
	} else {
		fprintf(stderr, "Usage: %s [scriptname]\n", argv[0]);
		exit(1);
	}
	
	// Ignore SIGPIPE
	struct sigaction ign;
	ign.sa_handler = SIG_IGN;
	sigemptyset(&ign.sa_mask);
	ign.sa_flags = SA_RESTART;
	sigaction(SIGPIPE, &ign, NULL);
	
	/*mask of SIGINT:*/
	sigset_t new;
	//new is a sigset with the new SIGINT signal
	//adding SIGINT to the new sigset
	sigemptyset(&new);
	sigaddset(&new, SIGINT);
	sigprocmask(SIG_BLOCK, &new, NULL);

	//creating a new thread 
	//for the clean_up fuction for Ctrl+C
	pthread_t clean_up_thread;
	pthread_create(&clean_up_thread, 0, clean_up, NULL);	
	
	db = db_new();
	
	char buf[1024];
	int id = 0;
	while (fgets(buf, 1024, stdin) != NULL) {
		if (strcmp(buf, "\n") == 0) {
			client_new(id);
			id++;
		}	
		
		
		if (strcmp(buf, "s\n") == 0) {
			pthread_mutex_lock(&cond_mutex);
			if_stopped = 1;    
			pthread_mutex_unlock(&cond_mutex);
		}
		
		if (strcmp(buf, "g\n") == 0) {
			pthread_mutex_lock(&cond_mutex);
			if_stopped = 0;
			pthread_cond_broadcast(&cond);
			pthread_mutex_unlock(&cond_mutex);
		}
	}
	
	//EOF received
	//the following code
	
	//first of all initializing all_cleanedup
	//meaning no threads are cleaned
	
	if (thread_counter == 0) {
		pthread_mutex_lock(&cleanup_cond_mutex);
		all_cleanedup = 1;
	} else {
		all_cleanedup = 0;
	}
	
	//creating a thread 
	//for the clean_up_fast fuction for Ctrl+D
	pthread_t clean_up_fast_thread;
	pthread_create(&clean_up_fast_thread, 0, clean_up_fast, NULL);	
	
	pthread_mutex_trylock(&cleanup_cond_mutex);
	//while nothing is cleaned up
	//block here
	while (all_cleanedup == 0) {
		pthread_cond_wait(&cleanup_cond, &cleanup_cond_mutex);
	}
	pthread_mutex_unlock(&cleanup_cond_mutex);
	
	
	//after all the clients were cleaned up
	//cancel the cleanup threads		
	pthread_cancel(clean_up_thread);
	pthread_cancel(clean_up_fast_thread);
	
	
	printf("Quitting\n");
	
	db_delete(db);
	free(scriptname);
	pthread_exit(0);
}

/***********************************
 * Client handling functions
 ***********************************/



/* Create a new client */
Client *client_new(int id) {
	pthread_t *thread_for_run = (pthread_t*)malloc(sizeof(pthread_t));
		
	//Tian Chen: adding up the thread_counter
	pthread_mutex_lock(&counter_mutex);
	//pthread_cleanup_push(&cleanup_pthread_mutex_unlock, (void *) &counter_mutex);
	
	thread_counter++;
	pthread_mutex_unlock(&counter_mutex);
	
	Client *client = (Client *)malloc(sizeof(Client));
	if(!client) {
		perror("malloc");
		return NULL;
	}

	// Create a window and set up a communication channel with it
	char title[20];
	sprintf(title, "Client %d", id);
	
	//Tian Chen:
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	if(!(client->window = window_new(title, scriptname))) {
		free(client);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		return NULL;
	}
	
	
	client->my_pthread = thread_for_run;
	
	
	
	pthread_create(thread_for_run, 0, run_client, client);	
	pthread_detach(*thread_for_run);
	//adding this newly created thread to the linkedlist
		
	add_pthread(thread_for_run);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	
	return client;
}

/* Delete a client and all associated resources */
void client_delete(Client *client) {
	window_delete(client->window);	
	
	pthread_mutex_lock(&counter_mutex);
	//pthread_cleanup_push(&cleanup_pthread_mutex_unlock, (void *) &counter_mutex);
	remove_pthread(((Client *) client)-> my_pthread);
	thread_counter--;
	//pthread_cleanup_pop(1);	
	pthread_mutex_unlock(&counter_mutex);
		
	free(client);
	
	
}





/* Function executed for a given client */
void *run_client(void *client) {
	char command[BUF_LEN];
	char response[BUF_LEN];
	
	//already pushed
	pthread_cleanup_push(&cleanup_pthread_client_delete, (void *) client);

	
	
	// Main loop of the client: fetch commands from window, interpret
	// and handle them, and send results to window.
	while(get_command(((Client *) client)->window, command)) {
		//first of all
		
		
		//running command
		pthread_mutex_lock(&cond_mutex);
		pthread_cleanup_push(&cleanup_pthread_mutex_unlock, (void *) &cond_mutex);
		
		
		while (if_stopped == 1) {
			pthread_cond_wait(&cond, &cond_mutex);
		}
		pthread_cleanup_pop(1);
		
		
		//here2
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		process_command(command, response);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
				
		if(!send_response(((Client *) client)->window, response)) break;	
		
	}
	
	
	pthread_cleanup_pop(0);
	//anything else we need to change here??
	
	//what's the meaning of these lines of code?
	//remove_pthread(((Client *) client)-> my_pthread);
	client_delete((Client *) client);
		
	return NULL;
}

/***********************************
 * Command processing functions
 ***********************************/

char *skip_ws(char *str);
char *skip_nonws(char *str);
void next_word(char **curr, char **next);

/* Process the given command and produce an appropriate response */
void process_command(char *command, char *response) {
	char *curr;
	char *next = command;
	next_word(&curr, &next);

	if(!*curr) {
		strcpy(response, "no command");
	} else if(!strcmp(curr, "a")) {
		next_word(&curr, &next);
		char *name = curr;
		next_word(&curr, &next);

		if(!*curr || *(skip_ws(next))) {
			strcpy(response, "ill-formed command");
		} else if(db_add(db, name, curr)) {
			strcpy(response, "added");
		} else {
			strcpy(response, "already in database");
		}
	} else if(!strcmp(curr, "q")) {
		next_word(&curr, &next);

		if(!*curr || *(skip_ws(next))) {
			strcpy(response, "ill-formed command");
		} else if(!db_query(db, curr, response, BUF_LEN)) {
			strcpy(response, "not in database");
		}
	} else if(!strcmp(curr, "d")) {
		next_word(&curr, &next);

		if(!*curr || *(skip_ws(next))) {
			strcpy(response, "ill-formed command");
		} else if(db_remove(db, curr)) {
			strcpy(response, "deleted");
		} else {
			strcpy(response, "not in database");
		}
	} else if(!strcmp(curr, "p")) {
		next_word(&curr, &next);

		if(!*curr || *(skip_ws(next))) {
			strcpy(response, "ill-formed command");
		} else {
			FILE *foutput = fopen(curr, "w");
			if (foutput) {
				db_print(db, foutput);
				fclose(foutput);
				strcpy(response, "done");
			} else {
				strcpy(response, "could not open file");
			}
		}
	} else if(!strcmp(curr, "f")) {
		next_word(&curr, &next);

		if(!*curr || *(skip_ws(next))) {
			strcpy(response, "ill-formed command");
		} else {
			FILE *finput = fopen(curr, "r");
			if(finput) {
				while(fgets(command, BUF_LEN, finput) != 0)
					process_command(command, response);

				fclose(finput);

				strcpy(response, "file processed");
			} else {
				strcpy(response, "could not open file");
			}
		}
	} else {
		strcpy(response, "invalid command");
	}
}

/* Advance pointer until first non-whitespace character */
char *skip_ws(char *str) {
	while(isspace(*str))
		str++;
	return str;
}

/* Advance pointer until first whitespace or null character */
char *skip_nonws(char *str) {
	while(*str && !(isspace(*str)))
		str++;
	return str;
}

/* Advance to the next word and null-terminate */
void next_word(char **curr, char **next) {
	*curr = skip_ws(*next);
	*next = skip_nonws(*curr);
	if(**next) {
		**next = 0;
		(*next)++;
	}
}