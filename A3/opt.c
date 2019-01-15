#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"
#include "sim.h"

//extern int memsize;

extern int debug;

extern struct frame *coremap;


struct linked_ref{
	addr_t addr;	
	struct linked_ref *next;
};

struct linked_ref *current_ref;


/* Page to evict is chosen using the optimal (aka MIN) algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int opt_evict() {
	int longest_unuse_page_index = 0;
	int longest_unuse_page_refCount = 0;
	struct linked_ref *next_ref = NULL;

	for (int i=0; i<memsize; i++){
		int gap_to_future_reference = 0;
		next_ref = current_ref;

		// Find the nearest use in the future. Note: If won't be used in future, choose of victim.
		while(next_ref->addr != coremap[i].address){
			// End of linked list
			if (next_ref->next == NULL){
				return i;
			}
			// Check next node in linked list.
			gap_to_future_reference++;
			next_ref = next_ref->next;
		}

		// Compare and update.
		if (gap_to_future_reference > longest_unuse_page_refCount){
			longest_unuse_page_refCount = gap_to_future_reference;
			longest_unuse_page_index = i;
		}
	}

	return longest_unuse_page_index;
}

/* This function is called on each access to a page to update any information
 * needed by the opt algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void opt_ref(pgtbl_entry_t *p) {
	// Simply go to the next node of linked list.
	current_ref = current_ref->next;
	return;
}

/* Initializes any data structures needed for this
 * replacement algorithm.
 */
void opt_init() {
	// Create a linked list of reference from tracefile
	// Initializes current_ref
	current_ref = NULL;

	// Open tracefile, the tracefile entry is from "sim.h" line 24.
	FILE *fp;
	if((fp = fopen(tracefile, "r")) == NULL) {
		perror("Error opening tracefile.\n");
		exit(1);
	}

	// Create linked list of reference, one line = one reference
	char buffer[MAXLINE];
	addr_t address = 0;
	char ref_type;
	//struct linked_ref *current = NULL;
	struct linked_ref *next = NULL;

	while(fgets(buffer, MAXLINE, fp) != NULL) {
		// Check first char, shouldn't be "="
		if (buffer[0] == '=')
			continue;

		// Extract information
		sscanf(buffer, "%c %lx", &ref_type, &address);

		// Create new node in linked list
		struct linked_ref *new_ref = malloc(sizeof(struct linked_ref));
		new_ref->addr = address;
		new_ref->next = NULL;
		if(!(current_ref == NULL)){
			next->next = new_ref;
			next = new_ref;
		}
		else{
			current_ref = new_ref;
			next = new_ref;
		}

	}


	// Close file, end of task.
	fclose(fp);
}
