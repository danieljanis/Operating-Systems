#ifndef SHARED_H
#define SHARED_H

/* 
Author: Daniel Janis
Program: Project 5 - CS 4760-002
Date: 11/19/20
File: shared.h
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/msg.h>

#define PROC_LIMIT 18
#define RESOURCE_LIMIT 20
#define MIN_S_RESOURCE (int)(RESOURCE_LIMIT*0.15) // Minimum sharable resource
#define MAX_S_RESOURCE (int)(RESOURCE_LIMIT*0.25) // Maximum sharable resource

struct Msgbuf {
    long mtype; // type of message being passed (explained in code)
    int mflag; // to send a msg to a specific process
    bool mrequest; // true for request, false for release
    bool mterminated; // true if it terminated, false if it didnt
    bool msafe; // true if safe state, false if unsafe state
    int mresource_count; // Number of resources in the specific resource class to be requested/released
    int mresource_index; // Index of the resource class being requested/released
    int mpcb_index;
};

struct Shmem {
    unsigned int sec; // holds seconds
    unsigned int nsec; // holds nanoseconds
    int shmPID; // Indicate when child processes have terminated
    int pgid; // Holds the process group ID, for termination
    int claim[PROC_LIMIT][RESOURCE_LIMIT]; // Max claims matrix (C)
    int alloc[PROC_LIMIT][RESOURCE_LIMIT]; // Allocation matrix (A)
    int needed[PROC_LIMIT][RESOURCE_LIMIT]; // "max needed in future" matrix (C-A)
    int initial[RESOURCE_LIMIT]; // Number of initialized resources (1-10)
    int available[RESOURCE_LIMIT]; // Number of available resources
    int sharable[RESOURCE_LIMIT]; // 1's in the sharable resources indexes
};

#endif
