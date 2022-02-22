// ------------
// This code is provided solely for the personal and private use of
// students taking the CSC369H5 course at the University of Toronto.
// Copying for purposes other than this use is expressly prohibited.
// All forms of distribution of this code, whether as given or with
// any changes, are expressly prohibited.
//
// Authors: Bogdan Simion
//
// All of the files in this directory and all subdirectories are:
// Copyright (c) 2019 Bogdan Simion
// -------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "executor.h"

extern struct executor tassadar;


/**
 * Populate the job lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <type> <num_resources> <resource_id_0> <resource_id_1> ...
 *
 * Each job is added to the queue that corresponds with its job type.
 */
void parse_jobs(char *file_name) {
    int id;
    struct job *cur_job;
    struct admission_queue *cur_queue;
    enum job_type jtype;
    int num_resources, i;
    FILE *f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", &id, (int*) &jtype, (int*) &num_resources) == 3) {

        /* construct job */
        cur_job = malloc(sizeof(struct job));
        cur_job->id = id;
        cur_job->type = jtype;
        cur_job->num_resources = num_resources;
        cur_job->resources = malloc(num_resources * sizeof(int));

        int resource_id; 
				for(i = 0; i < num_resources; i++) {
				    fscanf(f, "%d ", &resource_id);
				    cur_job->resources[i] = resource_id;
				    tassadar.resource_utilization_check[resource_id]++;
				}
				
				assign_processor(cur_job);

        /* append new job to head of corresponding list */
        cur_queue = &tassadar.admission_queues[jtype];
        cur_job->next = cur_queue->pending_jobs;
        cur_queue->pending_jobs = cur_job;
        cur_queue->pending_admission++;
    }

    fclose(f);
}

/*
 * Magic algorithm to assign a processor to a job.
 */
void assign_processor(struct job* job) {
    int i, proc = job->resources[0];
    for(i = 1; i < job->num_resources; i++) {
        if(proc < job->resources[i]) {
            proc = job->resources[i];
        }
    }
    job->processor = proc % NUM_PROCESSORS;
}


void do_stuff(struct job *job) {
    /* Job prints its id, its type, and its assigned processor */
    printf("%d %d %d\n", job->id, job->type, job->processor);
}


/**
 * Helper Functions
 */

int compare( const void* a, const void* b)
{
     int int_a = * ( (int*) a );
     int int_b = * ( (int*) b );

     if ( int_a == int_b ) return 0;
     else if ( int_a < int_b ) return -1;
     else return 1;
}
/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the executor
 * before any jobs start coming
 * 
 */
void init_executor() {
    int i;
    //initialize every resource lock
    for (i=0;i<NUM_RESOURCES;i++){
        pthread_mutex_init(&(tassadar.resource_locks[i]), NULL);
    }

    //initialize every resource tracker
    for(i=0;i<NUM_PROCESSORS;i++){
        tassadar.resource_utilization_check[i] = 0;
    }

    //initialize every admission queues
    for (i=0;i<NUM_QUEUES;i++){
        pthread_mutex_init(&(tassadar.admission_queues[i].lock), NULL);
        tassadar.admission_queues[i].pending_jobs = NULL;
        tassadar.admission_queues[i].pending_admission = 0;
        tassadar.admission_queues[i].capacity = QUEUE_LENGTH;
        tassadar.admission_queues[i].admitted_jobs = malloc(QUEUE_LENGTH*sizeof(struct job *));
        tassadar.admission_queues[i].num_admitted = 0;
        tassadar.admission_queues[i].head = 0;
        tassadar.admission_queues[i].tail = 0;
    }

    //initialize every processor record
    for(i=0;i<NUM_PROCESSORS;i++){
        pthread_mutex_init(&(tassadar.processor_records[i].lock), NULL);
        tassadar.processor_records[i].num_completed = 0;
        tassadar.processor_records[i].completed_jobs = NULL;
    }

}


/**
 * TODO: Fill in this function
 *
 * Handles an admission queue passed in through the arg (see the executor.c file). 
 * Bring jobs into this admission queue as room becomes available in it. 
 * As new jobs are added to this admission queue (and are therefore ready to be taken
 * for execution), the corresponding execute thread must become aware of this.
 * 
 */
void *admit_jobs(void *arg) {
    struct admission_queue *q = (struct admission_queue*) arg;
    while(q->pending_admission > 0){
        pthread_mutex_lock(&q->lock);
        while(q->capacity == q->num_admitted){
            pthread_cond_wait(&q->admission_cv, &q->lock);
        }
        //remove the end of pending_jobs
        struct job *curr_job;
        curr_job = q->pending_jobs;
        q->pending_jobs = q->pending_jobs->next;
        curr_job->next = NULL;
        q->pending_admission--;
        //add it to the end of admitted_jobs
        q->admitted_jobs[q->tail] = curr_job;
        q->tail = (q->tail+1)%QUEUE_LENGTH;
        q->num_admitted ++;
        pthread_cond_signal(&q->execution_cv);
        pthread_mutex_unlock(&q->lock);
    }

    return NULL;
}


/**
 * TODO: Fill in this function
 *
 * Moves jobs from a single admission queue of the executor. 
 * Jobs must acquire the required resource locks before being able to execute. 
 *
 * Note: You do not need to spawn any new threads in here to simulate the processors.
 * When a job acquires all its required resources, it will execute do_stuff.
 * When do_stuff is finished, the job is considered to have completed.
 *
 * Once a job has completed, the admission thread must be notified since room
 * just became available in the queue. Be careful to record the job's completion
 * on its assigned processor and keep track of resources utilized. 
 *
 * Note: No printf statements are allowed in your final jobs.c code, 
 * other than the one from do_stuff!
 */
void *execute_jobs(void *arg) {
    struct admission_queue *q = (struct admission_queue*) arg;
    while(q->num_admitted > 0 || q->pending_admission > 0){
        pthread_mutex_lock(&q->lock);
        while(q->num_admitted == 0){
            pthread_cond_wait(&q->execution_cv, &q->lock);
        }
        //Get the first job in line
        struct job *curr_job = q->admitted_jobs[q->head];
        q->admitted_jobs[q->head] = NULL;
        q->head = (q->head+1) %QUEUE_LENGTH;
        curr_job->next = NULL;
        qsort(curr_job->resources, curr_job->num_resources, sizeof(int), compare);
        //Tried to get all required resources
        for (int i=0; i<curr_job->num_resources; i++){
            int request = curr_job->resources[i];
            pthread_mutex_lock(&tassadar.resource_locks[request]);
            tassadar.resource_utilization_check[request]--;
        }

        //Done. Release all resource lock obtained and add this job to processor record
        pthread_mutex_lock(&tassadar.processor_records[curr_job->processor].lock);
        do_stuff(curr_job);
        q->num_admitted--;
        for (int i=0; i<curr_job->num_resources; i++){
            int request = curr_job->resources[i];
            pthread_mutex_unlock(&tassadar.resource_locks[request]);
        }
        struct processor_record *curr_queue = &tassadar.processor_records[curr_job->processor];
        curr_job->next = curr_queue->completed_jobs;
        curr_queue->completed_jobs = curr_job;
        curr_queue->num_completed++;
        pthread_mutex_unlock(&tassadar.processor_records[curr_job->processor].lock);
        pthread_cond_signal(&q->admission_cv);
        pthread_mutex_unlock(&q->lock);
    }
    free(q->admitted_jobs);
    return NULL;
}
