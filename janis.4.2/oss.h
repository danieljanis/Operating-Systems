#ifndef OSS_H
#define OSS_H

/* 
Author: Daniel Janis
Program: Project 3 - CS 4760-002
Date: 11/5/20
File: oss.h
*/

#include <string>
#include <queue>

#define MAX_LINES 1994

struct Blocked_Time {
    int r;
    int s;
};

int dispatcher_does_work();
void adjust_clock();
void sig_handle(int);
void countdown_to_interrupt(int, std::string);
void clock(int, std::string);
void usage(std::string);
void free_memory();
void statistics();

#endif
