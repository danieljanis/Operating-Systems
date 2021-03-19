#ifndef SHARED_H
#define SHARED_H

/* 
Author: Daniel Janis
Program: Project 2 - CS 4760-002
Date: 10/8/20
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

enum state {idle, want_in, in_cs};

struct Shmem {
    int pgid;
    int n;
    int turn;
    char words[100][MAX_CANON];
    state flag[100];
};

#endif
