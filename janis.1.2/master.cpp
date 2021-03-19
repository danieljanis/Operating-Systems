/************************************************************************/
/*  Author: Daniel Janis                                                */
/*  Date: 9/22/2020                                                     */
/*  Purpose:                                                            */
/*  This programs purpose is to take input from a source (file) that    */
/*  will be funneled in line by line. This file should contain command  */
/*  lines which this program "proc_fan" will execute, based on an -n    */
/*  option (handled in getopt below). This -n option allows for         */
/*  limiting the maximum allowable processes which can be running       */
/*  simultaneously. There is a limit of 1-27 maximum processes which    */
/*  can be running at once. The program forks a child, executes the     */
/*  command, and properly waits for child processes to terminate        */
/*  (or provides accurate error messages given the scenerio).           */
/************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include "master.h"
#include "shared.h"

// Instance of the struct shown above
struct shared_mem* shmem;

// Shared memory segment ID
int sem_id;

// Unique key to recognize the shared mem
key_t semkey;

// Max number of children processes allowed at a time
int pr_limit = 4;

// Actual number of active children processes present
int pr_count = 0;

// Number of children allowed to exist in the system at the same time
int ch_limit = 2;

int main(int argc, char *argv[]) {
    // SIGINT -- interrupt from keyboard
    signal(SIGINT, sig_handle);

    // Removes the ./ from the command-line invokation
    std::string exe_name;
    exe_name = argv[0];
    std::string ext = "/";
    if (exe_name.find(ext) != -1) {
        size_t findExt = exe_name.find_last_of("/");
        exe_name = exe_name.substr(findExt+1, exe_name.length());
    } // exe_name is the name of program that was called

    // generate a (key_t) type System V IPC key, for shmget
    if ((semkey = ftok("palin.cpp", 'p')) == (key_t) -1) {
        perror("IPC error: ftok");
        exit(1);
    }

    // If successful, it goes into the else section with a valid sem_id
    // IPC_CREAT -- create a semaphore set if it doesn't already exist in the kernel
    // 0666 - gives user permission to read and write but not execute
    if ((sem_id = shmget(semkey, sizeof(struct shared_mem), IPC_CREAT | 0666)) < 0) {
        perror("Bad... an error occurred, a valid shared memory identifier was not returned.\n");
        exit(1);
    }
    else {  
        // the struct is shared between the file mentioned in the ftok() call above involving semkey
        // and this file, master.cpp
        shmem = (struct shared_mem*) shmat(sem_id, NULL, 0); // attaches sem_id Sys V segment to the
                                                             //  addr space of the calling process
    }

    // For storing the funneled in files command-line arguments
    char temp[MAX_CANON];

    // The time a process will exist before terminating
    int timer = 100;

    // Childs process ID gets reset everytime this main() loops
    pid_t childpid = 0;

    // For storing the infile name
    std::string infile;

    std::string error_msg;
    // Checks that there are at least 3 arguments for valid invokation
    if (argc < 1) {
        usage(exe_name.c_str());
        error_msg = "Not enough arguments.\n";
        errors(exe_name.c_str(), error_msg.c_str());
        exit(2);
    }
    // This while loop + switch statement allows for the checking of parse options
    int opt;
    while ((opt = getopt(argc, argv, "n:s:t:h")) != -1) {
        switch (opt) {
            case 'n':
                // Number of max total child processes allowed by master
                pr_limit = atoi(optarg);
                // Checks the bounds on how many processes are allowed
                if (pr_limit <= 0 || pr_limit > 20) {
                    error_msg = "[-n x] value should be within 1-20 processes";
                    errors(exe_name.c_str(), error_msg.c_str());
                }
                break;
            case 's': // Number of children allowed to exist in system at the same time
                ch_limit = atoi(optarg);
                // Checks that the number of children allowed is positive, nonzero
                if (ch_limit <= 0) {
                    error_msg = "[-s x] value should be positive and nonzero";
                    errors(exe_name.c_str(), error_msg.c_str());
                }
                break;
            case 't': // where time is the max time a process will run before terminating
                timer = atoi(optarg);
                // Checks that the timer is positive and nonzero
                if (timer <= 0) {
                    error_msg = "[-t time] value should be positive and nonzero";
                    errors(exe_name.c_str(), error_msg.c_str());
                }
                break;
            case 'h': // -h will describe how the project should be run and then terminate
            default:
                usage(exe_name.c_str());
        }
    }
    printf("\nprocess limit: %d\n child limit: %d\n timer: %d\n", pr_limit, ch_limit, timer);
    if (argv[optind] == NULL) { // Checks for a file, required
        //free_memory();
        //usage(exe_name.c_str());
        error_msg = "There was no infile provided, terminating.\n";
        errors(exe_name.c_str(), error_msg.c_str());
        //exit(2);
    }
    
    // This is the filename being printed out
    infile = argv[optind];
    printf("optind: %s\n\n", argv[optind]);
    int count = 0;
    // This loop prints an error if too many arguments were passed, prints usage
    for (; optind < argc; optind++) {
        count++;
    }
    if (count != 1) {
        //free_memory();
        //usage(exe_name.c_str());
        error_msg = "Too many arguments were used, check the usage above.\n";
        errors(exe_name.c_str(), error_msg.c_str());
        //exit(2);
    }
    
    // Starts a countdown timer based on the [-t time] option (t will default to 100)
    countdown_to_interrupt(timer); 
    
    // Try and open the file given, exit on error
    char line[MAX_CANON]; // Store the line
    int word_count = 0; // Store the word count
    FILE *fptr;
    fptr = fopen(infile.c_str(), "r");
    if (fptr == NULL) {
        perror("Error on opening file");
        exit(EXIT_FAILURE);
    }
    else {
        // Prints the files contents, line by line, to standard output
        // (until end of file)
        while(!feof(fptr)) { // While not at the endo of file,
            if (fgets(line, MAX_CANON, fptr) == NULL) // 
                break;
            strcpy(shmem->words[word_count], line);
            word_count++;
        }
        fclose(fptr);
    }
    // This keeps track of the total amount of processes
    int total = 0;
    
    // if the number of strings in the file is less than the max allowable process limit,
    // set the pr_limit equal to the word_count
    if (word_count < pr_limit) {
        pr_limit = word_count;
    }  
    // The max amount of children allowed in the system at one time (ch_limit)
    //                          >
    // the maximum amount of processes ever allowed in the system (pr_limit)
    if (pr_limit < ch_limit) {
        ch_limit = pr_limit; // enforces the pr_limit (upper bound)
    }
    // Pass the process limit to the shared memory instance
    shmem->count = pr_limit;

    // Check if the process limit has been reached, if it hasn't,
    // try to spawn a child with the check_process_count() function
    while(total < ch_limit) {
        total++;
        check_process_count(total);
    }
    
    // wait for all processes to finish, keep trying to spawn new children if
    // necessary
    while (pr_count > 0) {
        wait(NULL);
        --pr_count;
        // Since the process count went down, try and spawn a new child
        total++;
        check_process_count(total);
    }
    // Free memory and close
    free_memory();
    return 0;
}

// Used to count down a pre-set timer until 0, then it sends interrupt signal
void countdown_to_interrupt(int secs) {
    clock(secs);
    struct sigaction act;
    // initializes the signal set to empty (sa_mask allows to specify signals
    // that aren't permitted to interrupt execution of this handler. Also,
    // the signal that caused the handler to be invoked is auto-added
    // to the process signal mask
    sigemptyset(&act.sa_mask);
    // Takes signal number as its only argument, specifies action
    act.sa_handler = &sig_handle;
    // Returning from a handler resume the library function
    act.sa_flags = SA_RESTART;
    // Sigaction returns -1 on an error, 0 on success
    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("Sigaction structure was not initialized properly");
    }
}

// This is used to create a timer
void clock(int secs) {
    struct itimerval time;
    // * it_value is the time until the timer expires and a signal gets sent
    // Configure the timer to "secs" whole seconds of elapsed time
    time.it_value.tv_sec = secs;
    // configure the timer to 0 microseconds, always less than one million
    time.it_value.tv_usec = 0;
    // * it_interval is the value to which the timer will be reset after it expires,
    // since both seconds and microseconds get set to 0 for interval,
    // the timer will be disabled once it expires.
    time.it_interval.tv_sec = 0;
    time.it_interval.tv_usec = 0;
    // This loop makes sure that the timer gets set or sends error
    if (setitimer(ITIMER_REAL, &time, NULL) == -1) {
        perror("Cannot arm the timer for the requested time");
    }
}

// Checks that there are less than 20 processes in the system
void check_process_count(int total_proc) {
    // If these two comparisons are both true, try and spawn another child
    if ((total_proc < pr_limit) && (pr_count < ch_limit)) {
        ++pr_count; // Increment total process counter
        if (fork() == 0) { // fork a child process
            if (total_proc == 1) 
                shmem->pid = getpid();
            setpgid(0, shmem->pid);
            char temp[MAX_CANON];
            // Group ID assigned and exec'd
            sprintf(temp, "%d", total_proc+1);
           // printf("%s\n", temp);
            execl("./palin", "palin", temp, (char*)NULL);
            perror("Failed to execl, exiting");
            exit(0);
        }
    }
}

// Kills all child processes and prints a log to log file, then terminates itself (frees memory)
void sig_handle(int signal) {
    // Sends a kill signal to the child process group
    killpg(shmem->pid, SIGTERM);
    free_memory();
    printf("CTRL+C was pressed in [master]\n");
    exit(0);
}

// Frees all shared memory
void free_memory() {
    // attaches shared memory segment to the address space of calling process
    shmdt(shmem);
    // IPC_RMID -- marks the segment to be destroyed. This will only occur after
    // the last process detaches it
    shmctl(sem_id, IPC_RMID, NULL);
}

// Prints an error message based on the calling executable
void errors(std::string name, std::string message) {
    printf("\n%s: Error: %s\n\n", name.c_str(), message.c_str());
    usage(name);
}

// Prints a usage message about how to properly use this program
void usage(std::string name) {
    free_memory();
    printf("\n%s: Usage: ./master [-n x] [-s x] [-t time] infile\n", name.c_str());
    printf("%s: Help:  ./master -h\n			[-h] will display how the project should be run and then, terminate.\n", name.c_str());
    printf("	[-n x] where x is the max total of child processes master will ever create. (Default 4)\n");
    printf("	[-s x] where x is the number of children allowed to exist at one time in the system. (Default 2)\n");
    printf("	[-t time] where time is the max time you want a process to run before terminating. (Default 100)\n");
    printf("	[infile] is the name of the input file containing strings to be tested.\n\n");
    exit(EXIT_FAILURE);
}
