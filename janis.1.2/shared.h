#ifndef SHARED_H
#define SHARED_H

#include <limits.h>

struct shared_mem {
    int count;
    int turn;
    int flag[20];
    char words[20][MAX_CANON];
    int pid;
};

#endif
