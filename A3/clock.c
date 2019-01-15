#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

int arm_pos;	// Position of "arm" in clock.

/* Page to evict is chosen using the clock algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int clock_evict() {
	int cnt=0;
	while(1){
		cnt++;
		// Check reference bit
		if (coremap[arm_pos].referenced == 0){
			return arm_pos;
		}
		else{ //.ref == 1
			coremap[arm_pos].referenced = 0;
		}

		// Update arm_pos
		arm_pos++;
		arm_pos %= memsize;

	}

	// NOTE: will never get to lines below. Bug occur if print statement is seen.
	printf("Never should you see me. I am in clock_evict() function.\n");
	return 0;
}

/* This function is called on each access to a page to update any information
 * needed by the clock algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void clock_ref(pgtbl_entry_t *p) {
	// Set ref-bit to one.
	int index = p -> frame >> PAGE_SHIFT;	// Get index of p
	coremap[index].referenced = 1;	// Set reference bit to 1.(no matter its previous value.)
	return;
}

/* Initialize any data structures needed for this replacement
 * algorithm.
 */
void clock_init() {
	// Initialize all ref-bit to be zero.
	for(int i = 0; i < memsize; ++i) {
		coremap[i].referenced = 0;
	}
	arm_pos = 0;
}
