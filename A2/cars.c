#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "traffic.h"

extern struct intersection isection;

/**
 * Populate the car lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <in_direction> <out_direction>
 *
 * Each car is added to the list that corresponds with
 * its in_direction
 *
 * Note: this also updates 'inc' on each of the lanes
 */
void parse_schedule(char *file_name) {
    int id;
    struct car *cur_car;
    struct lane *cur_lane;
    enum direction in_dir, out_dir;
    FILE *f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", &id, (int*)&in_dir, (int*)&out_dir) == 3) {

        /* construct car */
        cur_car = malloc(sizeof(struct car));
        cur_car->id = id;
        cur_car->in_dir = in_dir;
        cur_car->out_dir = out_dir;

        /* append new car to head of corresponding list */
        cur_lane = &isection.lanes[in_dir];
        cur_car->next = cur_lane->in_cars;
        cur_lane->in_cars = cur_car;
        cur_lane->inc++;
    }

    fclose(f);
}

/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the intersection
 * before any cars start coming
 *
 */
void init_intersection() {
    // Initializes locks for quadrants
    for (int i=0; i<4; i++){
        pthread_mutex_init(&isection.quad[i], NULL);
    }

    // Initialize lanes, aka entrance to intersection. (fixed size buffer)
    for (int i = 0; i < 4; i++) {
        // Initialize mutex and condition variable.
        pthread_mutex_init(&isection.lanes[i].lock, NULL);
		pthread_cond_init(&isection.lanes[i].producer_cv, NULL);
		pthread_cond_init(&isection.lanes[i].consumer_cv, NULL);
        // Initialize buffer.
		isection.lanes[i].buffer = malloc(sizeof(struct car *) * LANE_LENGTH);
        // Initialize other parameter for the lane
		isection.lanes[i].in_cars = NULL;
		isection.lanes[i].out_cars = NULL;
		isection.lanes[i].head = 0;
		isection.lanes[i].tail = 0;
		isection.lanes[i].capacity = LANE_LENGTH;
		isection.lanes[i].in_buf = 0;
        isection.lanes[i].inc = 0;
		isection.lanes[i].passed = 0;
	}
}

/**
 * TODO: Fill in this function
 *
 * Populates the corresponding lane with cars as room becomes
 * available. Ensure to notify the cross thread as new cars are
 * added to the lane.
 *
 */
void *car_arrive(void *arg) {
    struct lane *l = arg;

    while(1){
        pthread_mutex_lock(&l->lock);

        // Exit if no incoming cars.
        if ((l->in_cars == NULL) || (l->inc <= 0)) {
            pthread_cond_signal(&l->consumer_cv);
            pthread_mutex_unlock(&l->lock);
            return NULL;
        }

        // Wait if buffer full. (lane fully occupied)
        while (l->in_buf == l->capacity){
            pthread_cond_wait(&l->consumer_cv, &l->lock);
        }

        // Fetch the first waiting car from the list in_cars and add it to the entrance buffer.
        l->buffer[l->tail] = l->in_cars;  // place the first incoming car into entering buffer
        l->in_cars = l->in_cars->next;  // next incoming car becomes the first
        l->tail++;
        if (l->tail >= l->capacity){ 
            l->tail=0;
        }
        l->in_buf++;  // modify the counter
      
        pthread_cond_signal(&l->producer_cv);
        pthread_mutex_unlock(&l->lock);
    }

    /* avoid compiler warning */
    l = l;

    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Moves cars from a single lane across the intersection. Cars
 * crossing the intersection must abide the rules of the road
 * and cross along the correct path. Ensure to notify the
 * arrival thread as room becomes available in the lane.
 *
 * Note: After crossing the intersection the car should be added
 * to the out_cars list of the lane that corresponds to the car's
 * out_dir. Do not free the cars!
 *
 *
 * Note: For testing purposes, each car which gets to cross the
 * intersection should print the following three numbers on a
 * new line, separated by spaces:
 *  - the car's 'in' direction, 'out' direction, and id.
 *
 * You may add other print statements, but in the end, please
 * make sure to clear any prints other than the one specified above,
 * before submitting your final code.
 */
void *car_cross(void *arg) {
    struct lane *l = arg;
  
    while(1){
        // First CS begins. Lock the lane.
        pthread_mutex_lock(&l->lock);

        // Exit if all cars have passed.
        if (l -> inc == 0){
            pthread_mutex_unlock(&l->lock);
            return NULL;
        }

        // Wait if there are pending cars but buffer empty.
        while (l->in_buf == 0){
            pthread_cond_wait(&l->producer_cv, &l->lock);
        }

        // Now the lane buffer is non-empty, do the job.
        // First, get the current crossing car from lane buffer.
        struct car *current_car = l->buffer[l->head];
        l->head++;
        if (l->head >= l->capacity) { 
            l->head = 0;
        }
        l->inc--;
        l->in_buf--;
	
        // Signal producer for "buffer available".
        pthread_cond_signal(&l->consumer_cv);
        // Exit first CS, free the lock for lane.
        pthread_mutex_unlock(&l->lock);

        printf("%d %d %d\n", current_car->in_dir, current_car->out_dir, current_car->id);


        // Find the quadrants involved and lock them.
        int* path = compute_path(current_car->in_dir,current_car->out_dir);
        for (int i = 0; i < 4; i++){
            if (path[i] != 0){
                pthread_mutex_lock(&isection.quad[i]);
            }
        }
        // Get the lane to which the car leaves.
        struct lane* exit_lane = &isection.lanes[current_car->out_dir];

        // Second CS begins. Lock the exiting lane.
        pthread_mutex_lock(&exit_lane->lock);
        // Add current car to .out_car in exiting lane.
        current_car->next = exit_lane->out_cars;
        exit_lane->out_cars = current_car;
        exit_lane->passed++; //modify bookkeeping var
        // End of second CS, finish with exiting lane.
        pthread_mutex_unlock(&exit_lane->lock);

        // Unlock the region of passed car.
        for (int i = 0; i < 4; i++){
            if (path[i] != 0){
                pthread_mutex_unlock(&isection.quad[i]);
            }
        }
        free(path);
    }

    /* avoid compiler warning */
    l = l;

    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Given a car's in_dir and out_dir return a sorted
 * list of the quadrants the car will pass through.
 *
 */
int *compute_path(enum direction in_dir, enum direction out_dir) {
    /* Here the index of path add 1 equals to the quadrant number of the itersection,
       and the value it equals to is the oder of the car that is crossing the intersection.
       When path equals 0, it means that the car did not pass that quadrant of the itersection.
       For example: path={2,3,0,1} <=> first go to q4, then q1, then q2; won't reach q3. */

    // Check whether in_dir or out_dir is valid or not
    if (!(in_dir == NORTH || in_dir == SOUTH || in_dir == EAST || in_dir == WEST)) {
        fprintf(stderr, "Invalid in_dir.\n");
        exit(1);
    }
    if (!(out_dir == NORTH || out_dir == SOUTH || out_dir == EAST || out_dir == WEST)) {
        fprintf(stderr, "Invalid out_dir.\n");
        exit(2);
    }

    // U-turn is not allowed.
    if (in_dir ==  out_dir) {
        fprintf(stderr, "U-Turn is not allowed.\n");
        exit(3);
    }

    int* path = malloc(sizeof(int)*4);

    switch (in_dir) {
        case EAST:
            switch (out_dir) {
                case EAST:  // u-turn
                    path[0] = 1;
                    path[1] = 2;
                    path[2] = 3;
                    path[3] = 4;
                case SOUTH: // turn left
                    path[0] = 1;
                    path[1] = 2;
                    path[2] = 3;
                    path[3] = 0;
                case WEST:  // go straight
                    path[0] = 1;
                    path[1] = 2;
                    path[2] = 0;
                    path[3] = 0;
                case NORTH: // turn right
                    path[0] = 1;
                    path[1] = 0;
                    path[2] = 0;
                    path[3] = 0;
                default:
                    break;
            }

        case SOUTH:
            switch (out_dir) {
                case EAST:  // turn right
                    path[0] = 0;
                    path[1] = 0;
                    path[2] = 0;
                    path[3] = 1;
                case SOUTH: // u-turn
                    path[0] = 2;
                    path[1] = 3;
                    path[2] = 4;
                    path[3] = 1;
                case WEST:  // turn left
                    path[0] = 2;
                    path[1] = 3;
                    path[2] = 0;
                    path[3] = 1;
                case NORTH: // go straight
                    path[0] = 2;
                    path[1] = 0;
                    path[2] = 0;
                    path[3] = 1;
                default:
                    break;
            }

        case WEST:
            switch (out_dir) {
                case EAST:  // go straight
                    path[0] = 0;
                    path[1] = 0;
                    path[2] = 1;
                    path[3] = 2;
                case SOUTH: // turn right
                    path[0] = 0;
                    path[1] = 0;
                    path[2] = 1;
                    path[3] = 0;
                case WEST:  // u-turn
                    path[0] = 3;
                    path[1] = 4;
                    path[2] = 1;
                    path[3] = 2;
                case NORTH: // turn left
                    path[0] = 3;
                    path[1] = 0;
                    path[2] = 1;
                    path[3] = 2;
                default:
                    break;
            }

        case NORTH:
            switch (out_dir) {
                case EAST:  // turn left
                    path[0] = 0;
                    path[1] = 1;
                    path[2] = 2;
                    path[3] = 3;
                case SOUTH: // go straight
                    path[0] = 0;
                    path[1] = 1;
                    path[2] = 2;
                    path[3] = 0;
                case WEST:  // turn right
                    path[0] = 0;
                    path[1] = 1;
                    path[2] = 0;
                    path[3] = 0;
                case NORTH: // u turn
                    path[0] = 4;
                    path[1] = 1;
                    path[2] = 2;
                    path[3] = 3;
                default:
                    break;
            }
        default:
            break;
    }

    return path;
}
