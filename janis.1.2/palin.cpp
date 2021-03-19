#include <limits.h>
#include <unistd.h>
#include <cstdio>
#include <string.h>
#include <ctime>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <cstdlib>
#include "shared.h"

bool palin_check(char* temp);
char *strrev(char* string);
void sig_handler(int signal);
char *get_local_time();

// States for critical section problem, solve mutual exclusion
enum states { idle, trying, inside };

// Keeps track of process id at multiple points in program
int child_num;

int main(int argc, char ** argv) {
    bool is_palin = false;

    // Signal looks for CTRL+C to terminate
    signal(SIGTERM, sig_handler);
    
    // Initializes the word count, used for synchronization
    int word_count;

    if (argc != 2) { // something weird happened
        perror("Not enough arguments were passed");
        exit(EXIT_FAILURE);
    }
    else {
        // Grabs the process id
        child_num = atoi(argv[1]);
        word_count = child_num;
    }

    // Instance of shared memory
    struct shared_mem* shmem;
    // Shared memory segment ID
    int sem_id;
    // Unique key to recognize the shared mem
    key_t semkey;
    // Generate a key_t type System V IPC key, for shmget
    if ((semkey = ftok("palin.cpp", 'p')) == (key_t) -1) {
        perror("IPC error: ftok");
        exit(1);
    }
    
    // Try and allocate shared memory
    if ((sem_id = shmget(semkey, sizeof(struct shared_mem), IPC_CREAT | 0666)) < 0) {
        perror("Failed to allocate shared memory");
        exit(1);
    }
    else { // Successfully allocated shared memory
        shmem = (struct shared_mem*) shmat(sem_id, NULL, 0);
    }

    // Set the number of children allowed to be spawned
    int total_children = shmem->count+1;

    printf("[%s]: process: [%d], shared memory allocated successfully, ", get_local_time(), child_num);

    // Stores the word passed from shared memory
    printf("%d\n", word_count);
    char* string = shmem->words[word_count];
    
    // Removes any trailing newlines and replaces with null-terminator
    for (int i = 0; string[i] != '\0'; i++) {
        if (string[i] == '\n') {
            string[i] = '\0';
        }
    }
    // Checks if a string is a palindrome or not, returns true or false
    is_palin = palin_check(string);
    // If true, it is a palindrome, if false, it isn't a palindrome
    if (is_palin == true) {
            printf("PALINDROME! %s\n", string);
    }
    else if (is_palin == false) {
        printf("NON-palindrome! %s\n", string);
    }

    // CHECK IF ENTERING CRITICAL SECTION -----------------------------------------------
    int val;
    do {
        // Set the flag to wanting into critical section
        printf("---------------child_num-1== %d -------------\n", child_num-1);
        shmem->flag[child_num-1] = trying;
        // Get the turn from shared memory
        val = shmem->turn;
        printf("-----------------------val== %d -------------\n", val);
        // while it isn't the right turn,
        while(val != child_num-1) {
            if (shmem->flag[val] != idle) {
                val = shmem->turn;
            } 
            else {
                val = (val+1) % total_children;
            }
        }
        
        shmem->flag[child_num-1] = inside;
        
        // CHECK THAT NO-ONE ELSE IS IN THE CRITICAL SECTION 
        for (val = 0; val < total_children; val++) {
            if ((val != child_num-1) && (shmem->flag[val] == inside)) {
                break;
            }
        }
    }
    while((val < total_children) || ((shmem->turn != child_num-1) && (shmem->flag[shmem->turn] != idle)));
    shmem->turn = child_num-1;

    //-----------------------------------------------------------------------------------
    // INSIDE OF CRITICAL SECTION -------------------------------------------------------
    printf("[%s]: Process: %d,   now inside critical section!\n", get_local_time(), child_num);
    // Sleeps for a random amount of time between 0 to 2 seconds
    sleep(rand() % 3);
    // Prints the time again
    printf("[%s]: Process: %d,   leaving critical section\n", get_local_time(), child_num);
    //-----------------------------------------------------------------------------------

    val = (shmem->turn+1) % total_children;
    while (shmem->flag[val] == idle) {
        val = (val+1) % total_children;
    }

    shmem->turn = val;

    shmem->flag[child_num-1] = idle;

    printf("[%s]: Process: %d,   leaving palin.cpp... \n\n", get_local_time(), child_num);
    return 0;
}

// Returns true if "temp" is a valid palindrome, otherwise returns false
bool palin_check(char* temp) {
    int size = strlen(temp);
    char* backward = new char[size];
    strcpy(backward, temp);
    backward = strrev(backward);
    if (strcmp(temp, backward) ==  0) {
        return true;
    }
    else {
        return false;
    }
}

// This function reverses and returns the reversed string
char *strrev(char* string) {
    if (!string || ! *string)
        return string;
    int old_length = strlen(string) - 1;
    int new_length = 0;
    char ch;
    while (old_length > new_length) {
        ch = string[old_length];
        string[old_length] = string[new_length];
        string[new_length] = ch;
        old_length--;
        new_length++;
    }
    return string;   
}

// Handles any CTRL+C being pressed
void sig_handler(int signal) {
    if (signal == SIGTERM) {
        printf("CTRL+C was pressed in [palin]\n");
        exit(1);
    }
}

char *get_local_time() {
    // secs now holds the current time from system
    time_t secs = time(0);
    // gives a local time representation of timer
    struct tm * timeinfo = localtime(&secs);
    // Allocates memory for a char ptr to an array of chars
    char* time = new char[9];
    // formats the time
    strftime(time, 9, "%T", timeinfo);
    return time;
}
