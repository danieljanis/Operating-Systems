/*
 
	Author: Daniel Janis
	Program: Project 2 - Practice Shared Memory and IPC - CS 4760-002
	Date: 10/8/20
    File: master.cpp
	Purpose: 
     This program is designed to detect command-line arguments when called. These arguments
 	 allow the user to set the limits on various things explained with the [-h] command (check README).
 	 It then reads a file containing a list of potential palindromes (one string per line) into shared
 	 memory and fork/execs the child processes. There arguments below (pr_limit, ch_limit, timer, pr_count) 
 	 are all limiting factors in how this program should execute. When exec is called, the palin executable
 	 replaces the current process image with a new process image and begins executing.

*/

#include <sys/wait.h>
#include <sys/time.h>
#include <string.h>
#include "master.h"
#include "shared.h"

int pr_limit = 4; // max total of child processes master will ever create [-n x] (Default: 4)
int ch_limit = 2; // number of children allowed to exist in the system at the same time [-s x] (Default: 2)
int timer = 100; // time in seconds after which the process will terminate, even if it has not finished [-t time] (Default: 100)
int pr_count = 0; // Current running total of children processes present

struct Shmem* shmem; // struct instance used for shared memory

key_t skey; // Unique key for using shared memory
int sid; // Shared memory segment ID

int main(int argc, char *argv[]) {

    // Checks for interrupt from keyboard, if found, calls sig_handle()
    signal(SIGINT, sig_handle);

    int word_count = 0; // stores the total # of words in the file passed in

    // Used to store the name of the input file
    std::string infile;

    // If there is an error message, this helps us pass it
    std::string error_msg;

    // Remove the ./ from the command-line invokation
    std::string exe_name;
    exe_name = argv[0];
    std::string ext = "/";
    if (exe_name.find(ext) != -1) {
        size_t findExt = exe_name.find_last_of("/");
        exe_name = exe_name.substr(findExt+1, exe_name.length());
    } // exe_name is the name of program that was called

    // This while loop + switch statement allows for the checking of parse options
    int opt;
    while ((opt = getopt(argc, argv, "n:s:t:h")) != -1) {
        switch (opt) {
            case 'n':
                // Number of max total child processes allowed by master
                pr_limit = atoi(optarg);
                // Checks the bounds on how many processes are allowed
                if (pr_limit <= 0 || pr_limit > 27) {
                    error_msg = "[-n x] value should be within 1-27 processes.";
                    errors(exe_name.c_str(), error_msg.c_str());
                }
                break;
            case 's': // Number of children allowed to exist in system at the same time
                ch_limit = atoi(optarg);
                // Checks that the number of children allowed is positive, nonzero
                if (ch_limit <= 0 || ch_limit > 20) {
                    error_msg = "[-s x] value should be within 1-20 processes at the same time.";
                    errors(exe_name.c_str(), error_msg.c_str());
                }
                break;
            case 't': // where time is the max time a process will run before terminating
                timer = atoi(optarg);
                // Checks that the timer is positive and nonzero
                if (timer <= 0) {
                    error_msg = "[-t time] value should be positive and nonzero.";
                    errors(exe_name.c_str(), error_msg.c_str());
                }
                break;
            case 'h': // -h will describe how the project should be run and then terminate
            default:
                usage(exe_name.c_str());
        }
    }
    printf("\n______________________\n");
    printf("\n process limit: %d\n   child limit: %d\n         timer: %d\n", pr_limit, ch_limit, timer);
    printf("______________________\n\n");
    if (argv[optind] == NULL) { // Checks that there was a file provided, otherwise terminates
        error_msg = "There was no file provided, terminating.";
        errors(exe_name.c_str(), error_msg.c_str());
    }
    
    infile = argv[optind]; // Store the file name with the palindromes to test
    //printf("palindrome file: %s\n\n", infile.c_str());
    
    int count = 0;
    // After the optarg call, there should only be one filename (no other arguments)
    for (; optind < argc; optind++) {
        count++;
    }
    if (count != 1) { // Prints error message and terminates if too many arguments
        error_msg = "Too many arguments were passed, check the usage line below.";
        errors(exe_name.c_str(), error_msg.c_str());
    }

    // Generate a (key_t) type System V IPC key for use with shared memory
    if ((skey = ftok("makefile", 'a')) == (key_t) -1) {
        error_msg = exe_name + ": ftok : Error: there was an error creating a shared memory key (returned -1)"; 
        perror(error_msg.c_str());
        exit(EXIT_FAILURE);
    }

    // Tries to allocate a shared memory segment, otherwise it will use the 
    // previously allocated shared memory segment
    if ((sid = shmget(skey, sizeof(struct Shmem), IPC_CREAT | 0666)) < 0) { 
        // if the sid is < 0, it couldn't allocate a shared memory segment
        error_msg = exe_name + ": shmget: Error: An error occurred while trying to allocate a valid shared memory segment";
        perror(error_msg.c_str());
        exit(EXIT_FAILURE);
    }
    else {
        shmem = (struct Shmem*) shmat(sid, NULL, 0); 
        // attaches to Sys V shared mem segment using previously allocated memory segment (sid)
        // Since shmaddr is NULL, system chooses a suitable (unused) page-aligned address to attach the segment
    }
    
    // Counts down a preset random timer until 0, then sends interrupt signal
    countdown_to_interrupt(timer, exe_name.c_str()); 
   
    char temp[MAX_CANON]; // Temp storage for the line being read from infile 

    FILE *fptr;
    fptr = fopen(infile.c_str(), "r");
    if (fptr == NULL) {
        error_msg = exe_name + ": Error: Cannot open infile for reading, terminating";
        perror(error_msg.c_str());
        exit(EXIT_FAILURE);
    }
    else {
        while(!feof(fptr)) { // while not at the end of file,
            if (fgets(temp, MAX_CANON, fptr) == NULL) // Grab a mew word (if it isn't NULL)
                break; // Break if the line is NULL
            strcpy(shmem->words[word_count], temp); // Copies the word to the shared memory struct
            word_count++; // Increment word_count
        }
        fclose(fptr);
    }
    
    int running_procs = 0; // Keeps track of the total amount of processes

    // Sets a cap on the process limit equal to the word count (there can't be more processes than words)
    if (word_count < pr_limit) {
        pr_limit = word_count; // (the process limit cannot be bigger than the word count)
    }
    // Number of children allowed to exist cannot be greater than the max of child processes EVER created
    if (pr_limit < ch_limit) {
        ch_limit = pr_limit; // (there can't be more children in the system than the pr_limit allows)
    }
    
    // Pass the process limit to shared memory
    shmem->n = pr_limit; 

    // While the # of running processes is lower than the # of children allowed to exist at the same time,
    while (running_procs < ch_limit) {
        // Try to spawn another child
        running_procs++;
        try_spawn(running_procs, exe_name.c_str());
    }

    // Wait for all processes to finish, keep trying to spawn new children
    while (pr_count > 0) { // While there are currently children processes present,
        wait(NULL); // wait for them to finish
        --pr_count; // Decrement the number of current children processes present
        // Try to spawn another child
        running_procs++;
        try_spawn(running_procs, exe_name.c_str()); // Try to spawn children processes 
    }

    free_memory(); // Clears all shared memory
    return 0;
}

void try_spawn(int running_procs, std::string exe_name) {
    std::string error_msg;
    // If the number of running processes is less than or equal to the number of 
    // max child processes master will ever create, AND there are currently less children 
    // processes running than the ch_limit (Max number of child processes master will ever create)
    if ((running_procs <= pr_limit) && (pr_count < ch_limit)) {
        ++pr_count; // Increment process count
        if (fork() == 0) { // Try to fork a child
            // Fork was successful
            if (running_procs == 1) {// Tests that there is at least 1 running process
                shmem->pgid = getpid(); // Set the pgid in shared memory to the pgid of parent process
            }
            setpgid(0, shmem->pgid); // Creates a new process group within the session of calling processes
            char unique[MAX_CANON];
            sprintf(unique, "%d", running_procs); // Stores the number of running processes as unique
            execl("./palin", "palin", unique, (char*)NULL); // exec the forked child, passing # of running_procs
            error_msg = exe_name + ": Error: Failed to execl";
            perror(error_msg.c_str());
            exit(EXIT_FAILURE);
        }
    }
}

// Kills all child processes and terminates, and prints a log to log file and frees shared memory
void sig_handle(int signal) {
    //printf("signal: %d\n", signal);
    if (signal == 2) {
        printf("\n[master]: CTRL+C was received, interrupting process!\n"); 
    }
    if (signal == 14) { // "wake up call" for the timer being done
        printf("\n[master]: The countdown timer [-t time] has ended, interrupting process!\n", signal);
    }
    killpg(shmem->pgid, SIGTERM); // Sends a kill signal to the child process group
    free_memory(); // clears all shared memory
    exit(0);
}

/* 
struct sigaction {
    void (*sa_handler) (int);
    void (*sa_sigaction) (int, siginfo_t *, void *);
    sigset_t sa_mask; // specifies a mask of signals which should be blocked during execution of
                      // the signal handling function. The signal which triggers the handler gets blocked too
    int sa_flags;
    void (*sa_restorer) (void);
};

This struct is shown on the sigaction(2) manual page and it can be
modified to change the action taken by a process on receipt of a 
specific signal, used below it is able to send a signal to sig_handle()
whenever the timer (given by option [-t time]) runs out of time.  */ 

// Countdown timer based on [-t time] in seconds
void countdown_to_interrupt(int seconds, std::string exe_name) {
    std::string error_msg;
    clock(seconds, exe_name.c_str());
    struct sigaction action; // action created using struct shown above
    sigemptyset(&action.sa_mask); // Initializes the signal set to empty
    action.sa_handler = &sig_handle; // points to a signal handling function,
              // which specifies the action to be associated with SIGALRM in sigaction()
    action.sa_flags = SA_RESTART; // Restartable system call
    // SIGALRM below is an asynchronous call, the signal raised when the time interval specified expires
    if (sigaction(SIGALRM, &action, NULL) == -1) { 
        // &action specifies the action to be taken when the timer is done
        // NULL means that the previous, old action, isn't saved
        error_msg = exe_name + ": Error: Sigaction structure was not initialized properly";
        perror(error_msg.c_str());
        exit(EXIT_FAILURE);
    }
}

// This is used to create a clock, helper function for countdown_to_interrupt()
void clock(int seconds, std::string exe_name) {
    std::string error_msg;
    struct itimerval time; 
    // .it_value = time until timer expires and signal gets sent
    time.it_value.tv_sec = seconds; // .tv_sec = configure timer to "seconds" whole seconds
    time.it_value.tv_usec = 0; // .tv_usec = configure timer to 0 microseconds
    // .it_interval = value the timer will be set to once it expires and the signal gets sent
    time.it_interval.tv_sec = 0; // configure the interval to 0 whole seconds
    time.it_interval.tv_usec = 0; // configure the interval to 0 microseconds
    if (setitimer(ITIMER_REAL, &time, NULL) == -1) { // makes sure that the timer gets set
        error_msg = exe_name + ": Error: Cannot arm the timer for the requested time";
        perror(error_msg.c_str());
    } // setitimer(ITIMER_REAL, ..., NULL) is a timer that counts down in real time,
      // at each expiration a SIGALRM signal is generated
}


// Prints an error message based on the calling executable
void errors(std::string name, std::string message) {
    printf("\n%s: Error: %s\n\n", name.c_str(), message.c_str());
    usage(name);
}

// Prints a usage message about how to properly use this program
void usage(std::string name) {
    printf("\n%s: Usage: ./master [-n x] [-s x] [-t time] infile\n", name.c_str());
    printf("%s: Help:  ./master -h\n                    [-h] will display how the project should be run and then terminate.\n", name.c_str());
    printf("    [-n x] where x is the max total of child processes master will ever create. (Default 4)\n");
    printf("    [-s x] where x is the number of children allowed to exist at one time in the system. (Default 2)\n");
    printf("    [-t time] where time is the max time you want the program to run before terminating. (Default 100)\n");
    printf("    [infile] is the name of the input file containing strings to be tested.\n\n");
    exit(EXIT_FAILURE);
}

// Takes the shared memory struct and frees the shared memory
void free_memory() {
    shmdt(shmem); // Detaches the shared memory of "shmem" from the address space of the calling process
    shmctl(sid, IPC_RMID, NULL); // Performs the IPC_RMID command on the shared memory segment with ID "sid"
    // IPC_RMID -- marks the segment to be destroyed. This will only occur after the last process detaches it.
}
