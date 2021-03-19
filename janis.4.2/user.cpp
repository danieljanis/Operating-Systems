/* 

	Author: Daniel Janis
	Program: Project 3 - Message Passing and Operating System Simulator - CS 4760-002
    Date: 11/5/20
    File: user.cpp
    Purpose: Child program

 This program gets the simulated PID that was passed from the main program and then it
 performs a calculation to see if it will terminate or not, where the chances to terminate
 are fairly low.

 If this random choice decides NOT to terminate, it goes into a while() loop where it will
 receive a message from the main program containing:
        1. simulated PID (which is why "simPID" was received from the execl in the main program)
              - this makes sure the msgrcv only looks for that simulated PID
        2. Priority level (this is the time quantum given to run)
 Once the message is received, a random 0 or 1 will decide if this child will run for its full
 quantum or for only part of its quantum (and then it calculates from [0, quantum] and sends
 this back in a message to the main program using msgsnd. Along with the quantum, if a process
 did not use its entire quantum, that means it got INTERRUPTED(1) and if a process did use its
 entire time then it would send back a RAN FULL QUANTUM(2), so that the main program knows how
 this child finished (1 or 2).

 If this random choice decides to TERMINATE(3), once the message is received containing how long
 of a time quantum this process was given, a random time from [0, quantum] is calculated
 representing how long this child process ran on the core before terminating. This random time 
 along with the message to termiante(3) get sent back to the main program using msgsnd.

 After all of this (if the choice was to terminate), this child process will terminate and
 free up the shared memory using shmdt(shmem);

*/

#include <cstring>
#include <ctime>
#include <time.h>
#include <string>
#include <ctype.h>
#include "user.h"
#include "shared.h"

// Shared memory
struct Shmem* shmem;
// Message queues
struct Msgbuf buf1, buf2;
key_t skey; // shared memory key
int sid; // shared memory id
key_t mkey; // message queue key
int mqid; // message queue id

int main(int argc, char *argv[]) {
    signal(SIGTERM, sig_handler); // Detects signals to terminate this process

    srand(getpid());
    
    int simPID = atoi(argv[1]); // Grabs the simulated PID from the execl command
    
    std::string error_msg; // Stores the error message

    /* ATTACH TO SHARED MEMORY */
    // Generate a (key_t) type System V IPC key for use with shared memory
    if ((skey = ftok("makefile", 'a')) == (key_t) -1) {
        perror("user: ftok: Error: There was an error creating a shared memory key");
        exit(EXIT_FAILURE);
    }
    // Tries to attach to identifier of previously allocated shared memory segment
    if ((sid = shmget(skey, sizeof(struct Shmem), 0)) == -1) { 
        // if the sid is < 0, it couldn't allocate a shared memory segment
        perror("user: shmget: Error: An error occurred while trying to allocate a valid shared memory segment");
        exit(EXIT_FAILURE);
    }
    else {
        shmem = (struct Shmem*) shmat(sid, NULL, 0); 
        // attaches to Sys V shared mem segment using previously allocated memory segment (sid)
    }

    /* SET UP MESSAGE QUEUE */
    // Generate a (key_t) type System V IPC key for use with message queue
    if ((mkey = ftok("makefile", 'b')) == (key_t) -1) {
        error_msg = "user: Message Queue: ftok: Error: there was an error creating a message queue key";
        perror(error_msg.c_str());
        exit(EXIT_FAILURE);
    }
    // Connect to the queue
    if ((mqid = msgget(mkey, 0644)) == -1) {
        error_msg = "user: Message Queue: msgget: Error: Cannot allocate a valid message queue";
        perror(error_msg.c_str());
        exit(EXIT_FAILURE);
    }

    // Data to be calculated below
    int probability; // 1-100
    int quantum = 0;
    const int prob_to_terminate = 10;
    int entire_quant; // a 0 or a 1

    // 10% of the time it terminates, 90% of the time it doesn't
    probability = rand() % 100 + 1;
    if (probability <= prob_to_terminate) { // This means that I will terminate if the random number from 1-100 was 
        probability = 1;                    // less than or equal to 10 (my probability to terminate)
    }
    else {                 // This means that I will not terminate because the random number
        probability = 0;   // was within the range of 11-100, meaning I shouldn't send a message saying I terminated
    }

    // NOT TERMINATING
    while (probability == 0) {
        // Receive the time quantum from the simulated PID
        if (msgrcv(mqid, &buf1, sizeof(buf1), simPID, 0) < 0) {
            error_msg = "user: msgrcv: Error: Message was not received";
            perror(error_msg.c_str());
            exit(EXIT_FAILURE);
        }
         
        entire_quant = rand() % 2; // 1 == ran full quantum, 0 = ran part of quantum

        quantum = buf1.priority; // Sets the size of the quantum based on the queue value that was passed

        shmem->shmPID = getpid(); // Sets the actual PID in shared memory

        if (entire_quant == 0) { // Did NOT use entire quantum
            int temp;
            buf2.priority = rand() % quantum + 1; // Ran for a range of [0, quantum] nanoseconds (interrupted)
            buf2.mflag = 1;                       // This flag means it was INTERRUPTED(1)
        }
        else if (entire_quant == 1) { // Did use its entire quantum
            buf2.priority = quantum; // Ran for a full quantum
            buf2.mflag = 2;          // This flag means it RAN FULL QUANTUM(2), used its timeslice
        }
        
        buf2.mtype = 1; // mtype = 1, what OSS is waiting for
        // SEND MESSAGE TO THE PARENT THAT CHILD PID HAS TERMINATED
        if (msgsnd(mqid, &buf2, sizeof(buf2), 0) < 0) {
            error_msg = "user: Error: msgsnd: the message did not send";
            perror(error_msg.c_str());
            exit(EXIT_FAILURE);
        }
    }

    // TERMINATING
    if (probability == 1) {
        // Receive the time quantum from the simulated PID
        if (msgrcv(mqid, &buf1, sizeof(buf1), simPID, 0) < 0) {
            error_msg = "user: msgrcv: Error: Message was not received";
            perror(error_msg.c_str());
            exit(EXIT_FAILURE);
        }
        quantum = buf1.priority; // Sets the size of the quantum to the full quantum

        shmem->shmPID = getpid(); // Sets the actual PID in shared memory

        buf2.priority = rand() % quantum + 1; // Since its terminating, it only runs for part of the quantum
        buf2.mflag = 3;                       // This flag means its TERMINATED(3)
        buf2.mtype = 1;                       // mtype = 1 , what OSS is waiting for
        // SEND MESSAGE TO THE PARENT THAT CHILD PID HAS TERMINATED
        if (msgsnd(mqid, &buf2, sizeof(buf2), 0) < 0) {
            error_msg = "user: Error: msgsnd: the message did not send";
            perror(error_msg.c_str());
            exit(EXIT_FAILURE);
        }
    }
    shmdt(shmem); // Detaches shared memory pointer from this child process
    return 0;
}

void sig_handler(int signal) { // CLOSES ANY RUNNING CHILDREN WHEN CTRL+C IS PRESSED, or when timer runs out
    if (signal == 15) {
        //printf("[USER]: %d -- Process forced to terminate!\n", getpid());
        exit(EXIT_FAILURE);
    }
}
