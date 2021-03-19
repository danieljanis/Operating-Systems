#ifndef PALIN_H
#define PALIN_H

/* 
Author: Daniel Janis
Program: Project 2 - CS 4760-002
Date: 10/8/20
File: palin.h
*/

void process(const int, bool);
void critical_section(int, bool);
void sig_handler(int);
bool palin_check(char*);
char *strrev(char*);
char *get_time();

#endif
