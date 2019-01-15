#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

// NOTE: read this - https://en.wikipedia.org/wiki/Page_replacement_algorithm
// NOTE: LRU comments - https://cs.nyu.edu/courses/spring09/V22.0202-002/lectures/lecture-20.html
int ref_time;

/* Page to evict is chosen using the accurate LRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int lru_evict() {
	// Find the index of page with minimum timestamp
	int least_ref_page_index;
	int least_ref_time = ref_time;	

	for (int i=0; i<memsize; i++){
		if (coremap[i].timestamp<least_ref_time){	
			least_ref_time = coremap[i].timestamp;
			least_ref_page_index = i;
		}
	}

	return least_ref_page_index;
}

/* This function is called on each access to a page to update any information
 * needed by the lru algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void lru_ref(pgtbl_entry_t *p) {
	// Refresh timestamp, increment reference time ref_time;
	coremap[p -> frame >> PAGE_SHIFT].timestamp = ref_time;
	ref_time++;

	return;
}


/* Initialize any data structures needed for this
 * replacement algorithm
 */
void lru_init() {
	// Initialize all timestamp to be zero
	for(int i = 0; i < memsize; ++i) {
		coremap[i].timestamp = 0;
	}

	ref_time = 0;//lowerBoundOf_ref_time = 0;
	return;
}
