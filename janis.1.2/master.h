/* Author: Daniel Janis */
/* Date: 9/22/2020      */
#ifndef MASTER_H
#define MASTER_H

#include <string>

void countdown_to_interrupt(int);
void clock(int);
void check_process_count(int);
void sig_handle(int);
void free_memory();
void errors(std::string, std::string);
void usage(std::string);
void error_help(std::string, std::string);

#endif
