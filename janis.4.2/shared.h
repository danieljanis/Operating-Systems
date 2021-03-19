#ifndef SHARED_H
#define SHARED_H

/* 
Author: Daniel Janis
Program: Project 3 - CS 4760-002
Date: 11/5/20
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

#define MAX_TIME_SEC 2
#define MAX_TIME_NSEC 1000000000
#define PROC_LIMIT 18
#define QUANT_1 10000000
#define QUANT_2 QUANT_1*2
#define QUANT_3 QUANT_2*2
#define QUANT_4 QUANT_3*2 

struct Pcb {
    int simPID; // local simulated pid
    int priority; // current priority
    unsigned int total_s; // time in system
    unsigned int total_ns;
    unsigned int CPU_s; // total CPU time used
    unsigned int CPU_ns;
    unsigned int burst_s; // total time used during last burst1
    unsigned int burst_ns;
    unsigned int start_s; // When this process was generated
    unsigned int start_ns;
    int resume_s;
    int resume_ns;
};

struct Msgbuf {
    long mtype; // type of message being passed (explained in code)
    int mflag; // the message to be passed (explained in code)
    int priority;
};

struct Shmem {
    unsigned int sec; // holds seconds
    unsigned int nsec; // holds nanoseconds
    Pcb procs[PROC_LIMIT]; // Array storing Process Control Blocks for each child process in the system
    int shmPID; // Indicate when child processes have terminated
    int pgid; // Holds the process group ID, for termination
};

#endif
