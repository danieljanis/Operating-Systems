/*
 
	Author: Daniel Janis
	Program: Project 3 - Message Passing and Operating System Simulator - CS 4760-002
	Date: 10/20/20
    File: oss.cpp
	Purpose:

        This program OSS is meant to fork and exec child processes until certain limits
        get reached. One of these limits is a total process limit of 100 processes. Once
        this program has fork and exec'd 100 processes, it terminates any that are still 
        running and then exits. Another criteria is the clock in shared memory, once this
        clock counts over 2 seconds, all running processes are to terminate and the program
        exits. If CTRL+C is pressed, at any point, the program will terminate all children
        and then exit.

        There is also a limit on the number of concurrent children allowed to be executing
        at once. Two message queues are used to protect the critical region, allowing only
        one child to be execute inside that critical region at once. Inside of OSS's critical
        region, the shared clock gets incremented by 100 nanoseconds.

        Once a message is received, and the child's PID has been consumed, a new child is
        allowed to enter the critical section. This continues until the criteria for termination
        is reached, as explaiend above.

*/

#include <sys/wait.h>
#include <sys/time.h>
#include <string.h>
#include "oss.h"
#include "shared.h"

int ch_limit = 5; // number of concurrent children allowed to exist in the system at the same time [-s x] (Default: 5)
int timer = 20; // time in seconds after which the process will terminate, even if it has not finished [-t time] (Default: 100)
int pr_count = 0; // Current running total of children processes present

struct Shmem* shmem; // struct instance used for shared memory
struct Msgbuf buf1, buf2; // struct instance used for message queue

key_t skey; // Unique key for using shared memory
int sid; // Shared memory segment ID

key_t mkey_send, mkey_rec; // Unique key for using message queue
int mqid_send, mqid_rec; // Message queue ID

int main(int argc, char *argv[]) {

    signal(SIGINT, sig_handle); // Sets the signal to use my signal handler function

    std::string logfile; // Stores the name of the logfile

    std::string error_msg; // Stores the error message

    std::string exe_name; // Stores the parent executable name
    exe_name = argv[0];
    std::string ext = "/"; // For finding the "./" and removing it (below)
    if (exe_name.find(ext) != -1) {
        size_t findExt = exe_name.find_last_of("/");
        exe_name = exe_name.substr(findExt+1, exe_name.length());
    }

    // This while loop + switch statement allows for the checking of parse options
    int opt;
    while ((opt = getopt(argc, argv, "c:l:t:h")) != -1) {
        switch (opt) {
            case 'c': // Number of children allowed to exist in system concurrently
                ch_limit = atoi(optarg);
                // Checks that the number of children allowed concurrently is positive and within the limits
                if (ch_limit <= 0 || ch_limit > 27) {
                    error_msg = "[-c x] value should be within 1-27 processes at the same time.";
                    errors(exe_name.c_str(), error_msg.c_str());
                }
                break;
            case 'l':
                logfile = optarg; // Stores the logfile if one was passed, otherwise this will error
                break;
            case 't': // where timer is the maximum time in seconds before this executable
                timer = atoi(optarg); // terminates itself and all children
                if (timer <= 0) { // Timer must be positive and nonzero
                    error_msg = "[-t time] value should be positive and nonzero.";
                    errors(exe_name.c_str(), error_msg.c_str());
                }
                break;
            case 'h': // -h will describe how the project is to be run and then terminates
            default:
                usage(exe_name.c_str());
        }
    }
    FILE *logptr; // Pointer to the file to be opened
    logptr = fopen(logfile.c_str(), "w"); // overwrites the file if it exists, or creates a new file
    if (logptr == NULL) { // Checks that the file is able to be opened
        logfile = "logfile.log"; // if the file CAN'T be opened, create a temporary logfile name!
        logptr = fopen(logfile.c_str(), "w"); // overwrites the file if it exists or creates a new one
        if (logptr != NULL) { // Checks that the file opened
            fclose(logptr); // if it opened, Close the default logfile name!
        }
    }
    else {
        fclose(logptr); // close the getopt() provided logfile name!
    }

    printf("\n______________________\n"); // Prints the getopt() details given from above
    printf("\n child limit: %d\n     logfile: %s\n       timer: %d\n", ch_limit, logfile.c_str(), timer);
    printf("______________________\n\n");
    if (argv[optind] != NULL) { // Makes sure that no extra command-line options were passed
        error_msg = "Too many arguments were passed, check the usage line below.";
        errors(exe_name.c_str(), error_msg.c_str());
    }
   
    /////////////////////////////////////////
    /* ALLOCATE OR ATTACH TO SHARED MEMORY */
    /////////////////////////////////////////

    // Generate a (key_t) type System V IPC key for use with shared memory
    if ((skey = ftok("makefile", 'a')) == (key_t) -1) {
        error_msg = exe_name + ": Shared Memory: ftok: Error: there was an error creating a shared memory key"; 
        perror(error_msg.c_str());
        exit(EXIT_FAILURE);
    }

    // Tries to allocate a shared memory segment, otherwise it will use the 
    // previously allocated shared memory segment
    if ((sid = shmget(skey, sizeof(struct Shmem), IPC_CREAT | 0666)) == -1) { 
        // if the sid is < 0, it couldn't allocate a shared memory segment
        error_msg = exe_name + ": Shared Memory: shmget: Error: An error occurred while trying to allocate a valid shared memory segment";
        perror(error_msg.c_str());
        exit(EXIT_FAILURE);
    }
    else {
        shmem = (struct Shmem*) shmat(sid, NULL, 0); 
        // attaches to Sys V shared mem segment using previously allocated memory segment (sid)
    }

    //////////////////////////
    /* SET UP MESSAGE QUEUE */
    //////////////////////////

    // Generate a (key_t) type System V IPC key for use with message queue
    if ((mkey_send = ftok("makefile", 'b')) == (key_t) -1) {
        error_msg = exe_name + ": Message Queue: ftok: Error: there was an error creating a message queue key";
        perror(error_msg.c_str());
        free_memory();
        exit(EXIT_FAILURE);
    }

    // Creates a System V message queue (for OSS to USER)
    if ((mqid_send = msgget(mkey_send, 0644 | IPC_CREAT)) == -1) {
        error_msg = exe_name + ": Message Queue: msgget: Error: Cannot allocate a valid message queue";
        perror(error_msg.c_str());
        free_memory();
        exit(EXIT_FAILURE);
    }

    // Generate a (key_t) type System V IPC key for use with message queue
    if ((mkey_rec = ftok("makefile", 'c')) == (key_t) -1) {
        error_msg = exe_name + ": Message Queue: ftok: Error: there was an error creating a message queue key";
        perror(error_msg.c_str());
        free_memory();
        exit(EXIT_FAILURE);
    }

    // Creates a System V message queue (for USER to OSS)
    if ((mqid_rec = msgget(mkey_rec, 0644 | IPC_CREAT)) == -1) {
		error_msg = exe_name + ": Message Queue: msgget: Error: Cannot allocate a valid message queue";
        perror(error_msg.c_str());
        free_memory();
        exit(EXIT_FAILURE);
    }

    ///////////////////////////
    /* START COUNTDOWN TIMER */
    ///////////////////////////

    countdown_to_interrupt(timer, exe_name.c_str()); // Start the countdown clock from the -t timer option

    //////////////////////////////////////////////////
    /* INITIALIZE THE SHARED MEMORY CLOCK */
    //////////////////////////////////////////////////

    shmem->sec = 0; // Initialize shmem clock seconds to 0
    shmem->nanosec = 0; // Initialize shmem clock nanosec to 0

    // Initialize the shared memory int that indicates when child processes terminate
    shmem->shmPID = 0; // When this is 0, a child process is allowed to run
    // When this is a positive value, a child process has recently terminated
    
    //////////////////////////////////////////////////////////////////////////////////////////
    /* fork off appropriate number of child processes until the termination criteria is met */
    //////////////////////////////////////////////////////////////////////////////////////////
 
    pid_t childpid; // child process to be
    int running_procs = 0;

    FILE *fptr;
    fptr = fopen(logfile.c_str(), "w");

    // This forks the number of concurrent children allowed in the system at once
    for (int i = 0; i < ch_limit; i++) {
        childpid = fork(); // Returns 0 if child created, 
        if (childpid == 0) { // Returns to the newly creates child process
            if (running_procs == 0) { // CREATES A PROCESS GROUP FOR LATER (Safely terminate all children on CTRL+C)
                shmem->pgid = getpid();
            }
            setpgid(0, shmem->pgid); // sets the PGID of the process to the shmem process ID
            execl("./user", NULL); // Execute the "user" executable on the new child process
            error_msg = exe_name + ": Error: Failed to execl";
            perror(error_msg.c_str());
            exit(EXIT_FAILURE);   
        }   
        if (childpid < 0) {
            error_msg = exe_name + ": Error: Failed to fork a child";
            perror(error_msg.c_str());
            exit(EXIT_FAILURE);
        }
        fprintf(fptr, "OSS: Creating new child pid %d at system clock time %d.%09d\n", childpid, shmem->sec, shmem->nanosec);
        //fprintf(stderr, "OSS: Creating new child pid %d at system clock time %d.%09d\n", childpid, shmem->sec, shmem->nanosec);
        ++pr_count; // a process has been started, total counter incremented
        ++running_procs; // a new process is now running, keep track of how many are also running 
    }
    while(1) {
        if (shmem->shmPID != 0) { // Back in the parent and the PID of the child was set, (MEANING: child has terminated)
            //fprintf(stderr, "[OSS] process counter: %d\n", pr_count);
   
            /* LOG THAT THE CHILD PROC HAS TERMINATED AT THE TIME ON SHARED CLOCK */
              
            fprintf(fptr, "OSS: Child pid %d is terminating at system clock time %d.%09d\n", shmem->shmPID, shmem->sec, shmem->nanosec);
            //fprintf(stderr, "OSS: Child pid %d is terminating at system clock time %d.%09d\n", shmem->shmPID, shmem->sec, shmem->nanosec);
            
            waitpid(shmem->shmPID, NULL, 0);
            shmem->shmPID = 0;
            --running_procs;
        } 
        // CRITICAL SECTION FROM OSS (increment the OSS clock by constant nanoseconds value)
        else { // Inrement timer because child process is still executing
            shmem->nanosec += 1100; // Increment nanoseconds by 100
            if (shmem->nanosec >= 1000000000) { // If nanoseconds can be converted to seconds,
               shmem->nanosec -= 1000000000; // this takes care of conversions
               shmem->sec += 1;
            }
            if (shmem->sec >= 2) { // When 2 seconds elapsed has passed,
                //fprintf(stderr, "[OSS] elapsed time: %d.%09d seconds\n", shmem->sec, shmem->nanosec);
                fprintf(stderr, "[OSS]: 2 seconds have passed in the simulated system, interrupting processes!\n");
                sig_handle(SIGTERM);
                break;
            }
        }

        // If the process count is 100, send a signal to terminate all children and itself
        if (pr_count >= 100 && running_procs <= ch_limit-1) { // HAVE WE HIT THE LIMIT OF 100 PROCESSES
            fprintf(stderr, "[OSS]: 100 total children processes reached, interrupting processes!\n", pr_count, running_procs);
            sig_handle(SIGTERM);
            break;
        }
        // If there are less processes than the concurrent limit running and less than 100 total processes, spawn children
        if ((running_procs < ch_limit) && (pr_count < 100)) {
            childpid = fork(); // Returns 0 if child created, 
            if (childpid == 0) { // Returns to the newly creates child process
                if (running_procs == 0/*ch_limit == 1*/) { // If only one concurrent process was allowed, this makes sure it can terminate successfully
                    shmem->pgid = getpid();
                }
                setpgid(0, shmem->pgid); // sets the PGID of the process to the shmem process ID
                execl("./user", NULL); // Execute the "user" executable on the new child process
                error_msg = exe_name + ": Error: Failed to execl";
                perror(error_msg.c_str());
                --pr_count; // If execl failed, allow another process to try to start
                exit(EXIT_FAILURE);
            }
            if (childpid < 0) {
                error_msg = exe_name + ": Error: Failed to fork a child";
                perror(error_msg.c_str());
                exit(EXIT_FAILURE);
            }
            fprintf(fptr, "OSS: Creating new child pid %d at system clock time %d.%09d\n", childpid, shmem->sec, shmem->nanosec);
            //fprintf(stderr, "OSS: Creating new child pid %d at system clock time %d.%09d\n", childpid, shmem->sec, shmem->nanosec);

            ++pr_count; // a process has been started, total counter incremented
            ++running_procs; // a new process is now running, keep track of how many are also running
        }

        static int count = 1;
        buf1.mflag = count; // Sends a different flag everytime (used for the seeding the random generator in USER)
        count++;
        if (msgsnd(mqid_send, &buf1, sizeof(buf1), IPC_NOWAIT) < 0) { // SEND a message from OSS to USER, enter the critical region
            error_msg = exe_name + ": Error: msgsnd: the message did not send";
            perror(error_msg.c_str());
            exit(EXIT_FAILURE);
        }
        if (msgrcv(mqid_rec, &buf2, sizeof(buf2), 0, 0) == -1) { // RECEIVE a message from USER in OSS, leave the critical region
            error_msg = "oss: msgrcv: Error: Message was not received";
            perror(error_msg.c_str());
            exit(EXIT_FAILURE);
        }
    }
    fclose(fptr);
    free_memory(); // Clears all shared memory
    return 0;
}

// Kills all child processes and terminates, and prints a log to log file and frees shared memory
void sig_handle(int signal) {
    if (signal == 2) {
        printf("\n[OSS]: CTRL+C was received, interrupting process!\n"); 
    }
    if (signal == 14) { // "wake up call" for the timer being done
        printf("\n[OSS]: The countdown timer [-t time] has ended, interrupting processes!\n");
    }

    if (killpg(shmem->pgid, SIGTERM) == -1) { // Tries to terminate the process group (killing all children)
        fprintf(stderr, "\n[OSS]: Could not terminate normally, pulling out the big guns.\n");
        free_memory(); // Clears all shared memory
        killpg(getpgrp(), SIGTERM); // If the above attempt to terminate the process group failed, this makes sure they all die
        while(wait(NULL) > 0); // Waits for them all to terminate
        exit(EXIT_SUCCESS);
    }
    while(wait(NULL) > 0);
    free_memory(); // clears all shared memory
    exit(EXIT_SUCCESS);
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
    printf("\n%s: Usage: ./oss [-c x] [-l filename] [-t z]\n", name.c_str());
    printf("%s: Help:  ./oss -h\n                    [-h] will display how the project should be run and then terminate.\n", name.c_str());
    printf("    [-c x] where x is the number of children allowed to exist at one time in the system. (Default: 5)\n");
    printf("    [-t z] where z is the max time you want the program to run before terminating. (Default: 20)\n");
    printf("    [-l filename] where filename is the name of the log file to output information. (Default: \"logfile.log\")\n\n");
    exit(EXIT_FAILURE);
}

// Takes the shared memory struct and frees the shared memory
void free_memory() {

    // Performs control operations on the sending message queue
    if (msgctl(mqid_send, IPC_RMID, NULL) == -1) { // Removes the message queue
        perror("oss: Error: Could not remove the mqid_send message queue");
        exit(EXIT_FAILURE);
    }
   
    // Performs control operations on the receiving message queue
    if (msgctl(mqid_rec, IPC_RMID, NULL) == -1) { // Removes the message queue
        perror("oss: Error: Could not remove the mqid_rec message queue");
        exit(EXIT_FAILURE);
    }

    shmdt(shmem); // Detaches the shared memory of "shmem" from the address space of the calling process
    shmctl(sid, IPC_RMID, NULL); // Performs the IPC_RMID command on the shared memory segment with ID "sid"
    // IPC_RMID -- marks the segment to be destroyed. This will only occur after the last process detaches it.
    printf("[OSS]: all shared memory and message queues freed up! terminating!\n");
}
