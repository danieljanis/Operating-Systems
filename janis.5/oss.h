#ifndef OSS_H
#define OSS_H

/* 
Author: Daniel Janis
Program: Project 5 - CS 4760-002
Date: 11/19/20
File: oss.h
*/

#include "shared.h"
#include <string>
#include <queue>

#define MAX_LINES 100000

bool check_to_block(int, int, int);
void max_needed_in_future();
bool safety_algorithm(int, int, int);
void print_matrices();
void increment_clock();
void adjust_clock();
void sig_handle(int);
void countdown_to_interrupt(int, std::string);
void clock(int, std::string);
void usage(std::string);
void free_memory();

#endif
