#ifndef SHARED_H
#define SHARED_H

/* 
Author: Daniel Janis
Program: Project 3 - CS 4760-002
Date: 10/20/20
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

struct Shmem {
    int sec; // holds seconds
    int nanosec; // holds nanoseconds
    int shmPID; // Indicate when child processes have terminated
    int pgid; // Holds the process group ID, for termination
};

struct Msgbuf {
    //int mtype; // Holds the type
    int mflag; // stores the message for message queue
};

#endif
