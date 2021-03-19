#ifndef OSS_H
#define OSS_H

/* 
Author: Daniel Janis
Program: Project 3 - CS 4760-002
Date: 10/20/20
File: oss.h
*/

#include <string>

void sig_handle(int);
void countdown_to_interrupt(int, std::string);
void clock(int, std::string);
void errors(std::string, std::string);
void usage(std::string);
void free_memory();

#endif
