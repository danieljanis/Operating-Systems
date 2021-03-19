#ifndef MASTER_H
#define MASTER_H

/* 
Author: Daniel Janis
Program: Project 2 - CS 4760-002
Date: 10/8/20
File: master.h
*/

#include <string>

void try_spawn(int, std::string);
void sig_handle(int);
void countdown_to_interrupt(int, std::string);
void clock(int, std::string);
void errors(std::string, std::string);
void usage(std::string);
void free_memory();

#endif
