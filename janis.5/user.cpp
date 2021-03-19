/* 

	Author: Daniel Janis
	Program: Project 5 - Resource Management - CS 4760-002
    Date: 11/19/20
    File: user.cpp
    Purpose: Child program

    Every time this program runs, the user process that it represents will generate
    an array of Maximum Claims (using a random number, from 0 to the maximum resources in
    its resource class descriptor (from OSS))

    This program continually looks at the shared memory clock and will eventually
    request or release resources based on a bound (acquire_resources). When the shared
    clock + this bound have been surpassed, this user_proc will be allowed to request
    or release resources. Originally, when this user_proc is allocated 0 resources for
    every resource class, it will have to make a request.

    Requests are made by sending a message on the message queue. After sending a message
    this user process waits around for a message back indicating that the request was granted.
    At this time, another random bound is created and this process will loop again until
    shared time + that random bound time have been surpassed.

    There is a 70% chance for a process to make a request, and a 30% chance for the process 
    to make a release of a resource. When requesting, this program makes sure that
    the maximum available resource count's do not get exceeded, so that we keep an accurate count.

    At random times from 0 to 250 ms, the process will check and see if it should terminate. If
    so, it should release all of its allocated resources by sending a message to OSS telling OSS
    which process had terminated. This can ONLY happen after the process has ran for AT LEAST
    ONE SECOND.

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
    
    int indexPCB = atoi(argv[1]); // Grabs the simulated PID from the execl command
    
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

    // Declare maximum claims
    int claims_left[RESOURCE_LIMIT]; // Copies claims array (to be modified locally)

    // Calculate the max claims matrix for this user process 
    for (int j = 0; j < RESOURCE_LIMIT; j++) {
        shmem->claim[indexPCB][j] = rand() % (shmem->initial[j] + 1);
        claims_left[j] = shmem->claim[indexPCB][j];
    }

    // To see if a process has ran for its random value of time
    int temp_clock_s;
    int temp_clock_ns;

    // Grab the current clock time (at time of creation)
    int user_clock_s = shmem->sec + 1; // Add 1 second
    int user_clock_ns = shmem->nsec;

    // The latest time to request/release (MUST PASS THIS TIME TO REQUEST/RELEASE)
    int acquire_resources_s = shmem->sec;
    int acquire_resources_ns = shmem->nsec + rand() % 1000000 + 0; // Grabs the current nano time and adds nanoseconds
    
    // Percentage chance to request a resource vs. release one
    int chance_to_request = 70;
    bool finished = false;
    bool terminate = false;
    bool request = false;
    bool release = false;
    bool has_resources = false;
    bool requests_left_over = true;
    int terminate_s;
    int terminate_ns;

    while(1) {

        // finished becomes true whenever this process has ran for 1 second + the random time generated above
        if (!finished) {
            // Grab THIS CHILD's new clock time
            temp_clock_s = shmem->sec;
            temp_clock_ns = shmem->nsec;
           
            // MAKE SURE THE PROCESS HAS RAN FOR AT LEAST 1 SECOND BEFORE DEALLOCATING ALL RESOURCES
            if ((temp_clock_ns - user_clock_ns) >= 1000000000) {
                finished = true;
            }
            else if ((temp_clock_s - user_clock_s) >= 1) {
                finished = true;
            }

            // Set the time to terminate
            if (finished) {
                terminate_s = shmem->sec;
                terminate_ns = shmem->nsec + rand() % 2500000000 + 0;
                if (terminate_ns >= 1000000000) {
                    terminate_ns -= 1000000000;
                    terminate_s += 1;
                }
            }
           
            // Make sure that this child has ran for at least the random value from "acquire_resources_ns", (random from 0 to 1ms)
            if (shmem->sec == acquire_resources_s && shmem->nsec >= acquire_resources_ns || shmem->sec > acquire_resources_s) {
                if (chance_to_request <= rand() % 100 + 1 && requests_left_over) { // If the random value from 1-100 was greater than
                                 // or equal to the percentage to request, AND there is available room to request, then we go in here
                    has_resources = true;

                    // Grabs a random resource from a random resource class and makes sure there are claims left over
                    do {
                        buf2.mresource_index = rand() % RESOURCE_LIMIT + 0;
                        buf2.mresource_count = rand() % (claims_left[buf2.mresource_index] + 1);
                    } while(buf2.mresource_count == 0);
                    claims_left[buf2.mresource_index] -= buf2.mresource_count; // Keeps accurate count of Max Claims available for future requests

                    // Check to see if there is room for more requests or not
                    requests_left_over = false;
                    for (int i = 0; i < RESOURCE_LIMIT; i++) {
                        if (claims_left[i] != 0) {
                            requests_left_over = true;
                            break;
                        }
                    }
                    // Get ready to send the request message
                    buf2.mflag = 2;
                    request = true;
                }
                // If the value from that random 1-100 above was LESS THAN the percentage to request, then we release a resource
                else if (has_resources) {
                    requests_left_over = true;

                    // Grabs a random resource from a random resource class (from the allocated resources row of this user process)
                    do {
                        buf2.mresource_index = rand() % RESOURCE_LIMIT + 0;
                        buf2.mresource_count = rand() % (shmem->alloc[indexPCB][buf2.mresource_index] + 1);
                    } while(buf2.mresource_count == 0);
                    claims_left[buf2.mresource_index] += buf2.mresource_count; // Keeps accurate count of Max claims available for future requests

                    // Check to see if this process has resources or not
                    has_resources = false;
                    for (int i = 0; i < RESOURCE_LIMIT; i++) {
                        if (shmem->alloc[indexPCB][i] != 0) {
                            has_resources = true;
                        }
                    }
                    // Get ready to send the release message
                    buf2.mflag = 3;
                    release = true;
                }
            }
        }
        else { // Process has ran for at least one second, check if it should terminate
            if (shmem->sec == terminate_s && shmem->nsec >= terminate_ns || shmem->sec > terminate_s) {
                buf2.mflag = 1;
                terminate = true;
            }
        }
        // Send the message to OSS concerning if a resource was requested/released or if this process terminated 
        buf2.mpcb_index = indexPCB;
        buf2.mtype = 1;
        msgsnd(mqid, &buf2, sizeof(buf2), 0);


        if (request == true) {
            // Message from OSS to see if its safe to request resources
            msgrcv(mqid, &buf1, sizeof(buf1), indexPCB+2, 0);
        }
        if (terminate) {
            break;
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
