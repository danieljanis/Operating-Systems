/*
 
	Author: Daniel Janis
	Program: Project 5 - Resource Management - CS 4760-002
	Date: 11/19/20
    File: oss.cpp
	Purpose: Main program

    This program will fork multiple children at random times. The randomness is simulated
    by a logical clock, stored in shared memory. At the start, this program allocates shared
    memory for different matrices which are used to determine if the state of the system will
    become deadlocked (if a certain resource request was granted, like if it will deadlock or not). There are
    a total of 20 resource descriptors and ~15-25% of them may be shared resources. Once the resource vector
    with resource descriptors has been created, a Max Claims array is formed, storing a random value
    from 1-10, which will be the initial instances for each resource class.

    After resources get set up, fork a user process at random times (from 1-500 ms of the logical clock).
    A bitvector (bitv) is used to make sure that only 18 processes are active in the system at once. 

    Finally, this program decides whether the received resource request should be allocated to the process
    or not. This is done using a deadlock avoidance (safety) algorithm. This algorithm makes sure that
    resources will NOT be allocated if that allocation could potentially lead to a deadlock.
     - If a process releases resources, it will update the allocated and available tables in shared memory and
       then see if it will be able to unblock any blocked processes without encountering a deadlock.
     - If a process requests resources and the algorithm says that the request would put the system into an
       UNSAFE state, then that process gets put into a blocked 2D array (which will periodically attempt
       to be unblocked, whenever a process releases any resources).

    TERMINATION CRITERIA: New processes stop being generated after 5 seconds real-time passes,
                          or when a total of 40 processes have been created and exited 
*/

#include <sys/wait.h>
#include <sys/time.h>
#include <string.h>
#include <bitset>
#include <vector>
#include <algorithm>
#include <iostream>
#include "oss.h"
//#include "shared.h"

static bool five_second_alarm = false;

// If we want verbose mode or not
static bool verbose = false;

// TO TRACK/TRUNCATE THE LOGFILE's LENGTH
static int line_count = 0;
//struct Blocked {
//    int blocked_process_index;
  //  int blocked_resource_index;
    //int blocked_count;
//};

//std::queue<int> queue;
//std::vector<struct Blocked> blocked;

//int pr_count = 0; // Process count

// Used to print a log with statistics at the very end
static std::string logfile = "logfile.log";

// OPEN LOG FILE FOR WRITING
static FILE *fptr;


static int blocked[PROC_LIMIT][2]; // Stores the process index, 
                               // and blocked[0] is for the resource index, 
                               // blocked[1] is for the resource count

// bitv KEEPS TRACK OF WHICH Pcb's ARE IN USE
static std::bitset<PROC_LIMIT> bitv;

// Shared memory
struct Shmem* shmem;

// Message queues
struct Msgbuf buf1, buf2;
key_t skey; // shared memory key
int sid; // shared memory id
key_t mkey; // message queue key
int mqid; // message queue id

int main(int argc, char *argv[]) {

    // If we want verbose mode or not
    //bool verbose = false;

    // Open logfile for writing
    fptr = fopen(logfile.c_str(), "w+");

    // Track the total number of processes created
    int total_procs = 0;
    
    // Track the current number of running user processes
    int current_procs = 0;
    
    // SEED THE RANDOM GENERATOR USING NANO-SEC's
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand((time_t) ts.tv_nsec);
    
    // WAITS FOR A SIGINT SIGNAL (CTRL+C from user)
    signal(SIGINT, sig_handle);

    // FOR FORKING
    pid_t childpid;

    // FOR ERROR HANDLING
    std::string error_msg; // Errors
    std::string exe_name; // Name of this executable
    
    // Gets the executable name and removes "./"
    exe_name = argv[0];
    std::string ext = "/";
    if (exe_name.find(ext) != -1) {
        size_t findExt = exe_name.find_last_of("/");
        exe_name = exe_name.substr(findExt+1, exe_name.length());
    }

    // Check if the verbose option was chosen
    int opt;
    while ((opt = getopt(argc, argv, "vh")) != -1) {
        switch (opt) {
            case 'v':
                verbose = true;
                break;
            case 'h':
            default:
                usage(exe_name.c_str());
        }
    }
    
    // ALLOCATE OR ATTACH TO SHARED MEMORY
    if ((skey = ftok("makefile", 'a')) == (key_t) -1) {
        error_msg = exe_name + ": Shared Memory: ftok: Error: there was an error creating a shared memory key"; 
        perror(error_msg.c_str());
        exit(EXIT_FAILURE);
    }
    if ((sid = shmget(skey, sizeof(struct Shmem), IPC_CREAT | 0666)) == -1) { 
        error_msg = exe_name + ": Shared Memory: shmget: Error: An error occurred while trying to allocate a valid shared memory segment";
        perror(error_msg.c_str());
        exit(EXIT_FAILURE);
    }
    else {
        shmem = (struct Shmem*) shmat(sid, NULL, 0); 
    }

    // SET UP MESSAGE QUEUE
    if ((mkey = ftok("makefile", 'b')) == (key_t) -1) {
        error_msg = exe_name + ": Message Queue: ftok: Error: there was an error creating a message queue key";
        perror(error_msg.c_str());
        free_memory();
        exit(EXIT_FAILURE);
    }
    // Create the queue
    if ((mqid = msgget(mkey, 0644 | IPC_CREAT)) == -1) {
        error_msg = exe_name + ": Message Queue: msgget: Error: Cannot allocate a valid message queue";
        perror(error_msg.c_str());
        free_memory();
        exit(EXIT_FAILURE);
    }

    // START COUNTDOWN TIMER
    int terminate_after = 5; // real life seconds to run the simulation
    countdown_to_interrupt(terminate_after, exe_name.c_str());

    // INITIALIZE THE SHARED MEMORY CLOCK
    shmem->sec = 0;
    shmem->nsec = 0;

    // Initialize 20 Resources
    for (int j = 0; j < RESOURCE_LIMIT; j++) {
        shmem->initial[j] = rand() % 10 + 1;
        shmem->available[j] = shmem->initial[j];
    }

    // Determines the number of sharable resources! 
    int num_shared = rand() % (MAX_S_RESOURCE - (MAX_S_RESOURCE - MIN_S_RESOURCE)) + MIN_S_RESOURCE;

    // Acts as a static bitvector for which resources are "sharable"
    while(num_shared != 0) {
        int random = rand() % (19+1) + 0; // Chooses randomly
        if (shmem->sharable[random] == 0) { // Does not repeat a choice
            shmem->sharable[random] = 1;
            num_shared -= 1;
        }
    }

    // Initialize timer to generate new processes
    unsigned int new_sec = 0;
    unsigned int new_nsec = rand() % 500000000 + 1000000; // Add the random time from 1-500 ms to "new_nsec"

    // Move the shared clock ahead to represent work being done to schedule the very first process
    shmem->nsec = new_nsec;

    // DO A NONBLOCKING WAIT ON MESSAGES (if there is no message, increment the clock)
    int pcb_index = -1;
    int simulated_PID = -1;
    bool bitv_is_open = false;
    int spawn_nsec;
    int granted_requests = 0;
    while(1) {
        // Handles breaking from this loop whenever termination criteria was reached
        if ((five_second_alarm || total_procs == 40) && current_procs == 0) {
            break;
        }

        // Print matrices every 5 granted requests
        if (granted_requests >= 5) {
            granted_requests = 0;
            print_matrices();
        }

        // When the shared clock has passed the time to launch a new process, it passes this check
        if (shmem->sec == new_sec && shmem->nsec >= new_nsec || shmem->sec > new_sec) {
            new_sec = 0;
            new_nsec = 0; // Reset the clock to fork new procs

            // Checks if the bitvector has any open spots
            for (int i = 0; i < bitv.size(); i++) {
                if (!bitv.test(i)) {
                    pcb_index = i;
                    simulated_PID = i+2;
                    bitv.set(i); // We are now using this PID
                    bitv_is_open = true;
                    break;
                }
            }

            // If there were open spots, generate a new child proc
            if (bitv_is_open && total_procs < 40 && !five_second_alarm) { // pr_count < 3 can be removed at final version
                
                // For sending the process index through execl
                char indexPCB[3];
                snprintf(indexPCB, 3, "%d", pcb_index);

                // Fork and then execl the child                
                childpid = fork();
                if (childpid == 0) {
                    shmem->pgid = getpid();
                    execl("./user_proc", "user_proc", indexPCB, '\0');
                    error_msg = exe_name + ": Error: failed to execl";
                    perror(error_msg.c_str());
                    exit(EXIT_FAILURE);
                }
                if (childpid < 0) {
                    error_msg = exe_name + ": Error: failed to fork a child";
                    perror(error_msg.c_str());
                    exit(EXIT_FAILURE);
                }
                total_procs++;
                current_procs++; // Gets decremented when a process terminates     
          
                // Closes this so that a new process must be vetted by the bitvector check above
                bitv_is_open = false; 
                
            }

            // Adds the shared clock + the random time from 1-500 ms (to temp variables new_sec and new_nsec)
            new_sec = shmem->sec;
            new_nsec = (rand() % 500000000 + 1000000) + shmem->nsec;

            // Correct the time to launch the NEXT PROCESS
            while (new_nsec >= 1000000000) {
                new_nsec -= 1000000000;
                new_sec += 1;
            } 
        }

        // Add time to the shared clock, simulating the system performing calculations (taking CPU time)
        increment_clock();

        int count = 0;
        // Receive a message from the user_proc
        int msg = msgrcv(mqid, &buf2, sizeof(buf2), 1, IPC_NOWAIT);
        if (msg > 0) {
            // Grab the index of the user_proc, to know who sent the message to this parent process
            pcb_index = buf2.mpcb_index;

            // TERMINATING 
            if (buf2.mflag == 1) {
                current_procs--; // Decrement because a process has terminated
                
                if (++line_count < MAX_LINES && verbose) {
                    fprintf(fptr, "OSS has acknowledged that P%d is terminating\n", pcb_index);
                }//fprintf(stderr,"OSS has acknowledged that P%d is terminating\n", pcb_index);
                if (++line_count < MAX_LINES && verbose) {
                    fprintf(fptr, "OSS releasing all resources for P%d\n", pcb_index);
                }//fprintf(stderr,"OSS releasing all resources for P%d\n", pcb_index);

                // Release all resources for this process, and reset the claims/allocated matrices
                for (int i = 0; i < RESOURCE_LIMIT; i++) {
                    shmem->available[i] += shmem->alloc[pcb_index][i];
                    shmem->alloc[pcb_index][i] = 0;
                    shmem->claim[pcb_index][i] = 0;
                }
                // Open up the spot in the bitvector for this process index
                bitv.reset(pcb_index);

                // See if it is safe to unblock any processes
                for (int i = 0; i < PROC_LIMIT; i++) {
                    if (blocked[i][1] > 0) { // If this blocked process requested more than zero resources (meaning it was blocked)
                        bool safety = safety_algorithm(i, blocked[i][0], blocked[i][1]);
                        if (safety) { // If the algorithm said that this BLOCKED process was okay to be UNBLOCKED,
                          
                            if (++line_count < MAX_LINES) {
                                fprintf(fptr, "OSS unblocking P%d at time %d:%d\n", i, shmem->sec, shmem->nsec);
                            }//fprintf(stderr,"OSS unblocking P%d at time %d:%d\n", i, shmem->sec, shmem->nsec);
                            if (++line_count < MAX_LINES && verbose) {                            
                                fprintf(fptr, "OSS granting P%d request for %d resources of type R%d at time %d:%09d\n", i, blocked[i][1], blocked[i][0], shmem->sec, shmem->nsec);
                            }//fprintf(stderr,"OSS granting P%d request for %d resources of type R%d at time %d:%09d\n", i, blocked[i][1], blocked[i][0], shmem->sec, shmem->nsec);
                            
                            // Track the number of total granted requests
                            granted_requests++;

                            // Set the Resource index and count, for adding to allocated and removing from available
                            int resource_index = blocked[i][0];
                            int resource_count = blocked[i][1];
                            
                            // Add the requested resource to the allocated vector for this process,
                            shmem->alloc[i][resource_index] += resource_count;

                            // Remove the # of requested resources from the available resources vector
                            shmem->available[resource_index] -= resource_count;

                            print_matrices();

                            // When these values are zero, the process is seen as "UNBLOCKED"
                            blocked[i][0] = 0;
                            blocked[i][1] = 0;
                            
                            // Send a message to this user process saying that it was granted the request and unblocked
                            buf1.mtype = i+2;
                            buf1.msafe = safety;
                            msgsnd(mqid, &buf1, sizeof(buf1), 0); 
                        }
                    }
                }
            }
            // REQUESTING
            else if (buf2.mflag == 2) {
                
                if (++line_count < MAX_LINES && verbose) {
                     fprintf(fptr, "OSS has detected process P%d requesting %d resources of type R%d at time %d:%09d\n", pcb_index, buf2.mresource_count, buf2.mresource_index, shmem->sec, shmem->nsec);
                }//fprintf(stderr,"OSS has detected process P%d requesting %d resources of type R%d at time %d:%09d\n", pcb_index, buf2.mresource_count, buf2.mresource_index, shmem->sec, shmem->nsec);

                if (++line_count < MAX_LINES && verbose) {
                    fprintf(fptr, "OSS running deadlock avoidance at time %d:%09d\n", shmem->sec, shmem->nsec);
                }//fprintf(stderr,"OSS running deadlock avoidance at time %d:%09d\n", shmem->sec, shmem->nsec);
                
                // Check if the request gets blocked or not
                bool safe = check_to_block(pcb_index, buf2.mresource_index, buf2.mresource_count);

                // For the message to be sent to the user_proc
                buf1.mtype = pcb_index+2;
                buf1.msafe = safe;
                   
                // If the request for this process index was safe, do the following 
                if (buf1.msafe == true) {
                    
                    if (++line_count < MAX_LINES && verbose) {
                        fprintf(fptr, "OSS granting P%d request for %d resources of type R%d at time %d:%09d\n", pcb_index, buf2.mresource_count, buf2.mresource_index, shmem->sec, shmem->nsec);  
                    }//fprintf(stderr,"OSS granting P%d request for %d resources of type R%d at time %d:%09d\n", pcb_index, buf2.mresource_count, buf2.mresource_index, shmem->sec, shmem->nsec);
                    
                    // Track the number of granted requests
                    granted_requests++;
                       
                    // Add the requested resource to the allocated vector for this process,
                    shmem->alloc[pcb_index][buf2.mresource_index] += buf2.mresource_count;

                    // Remove the # of requested resources from the available resources vector
                    shmem->available[buf2.mresource_index] -= buf2.mresource_count;

                    // Send a message to this user process saying that it was granted the request
                    msgsnd(mqid, &buf1, sizeof(buf1), 0);

                }// If the request was not safe, do the following         
                else {
                    if (++line_count < MAX_LINES) {
                        fprintf(fptr, "OSS blocking P%d (request denied for now) at time %d:%09d\n", pcb_index, shmem->sec, shmem->nsec);
                    }//fprintf(stderr,"OSS blocking P%d (request denied for now) at time %d:%09d\n", pcb_index, shmem->sec, shmem->nsec);
                }
            }
            // RELEASING
            else if (buf2.mflag == 3) { // Release of resources was requested
                
                if (++line_count < MAX_LINES && verbose) {
                    fprintf(fptr, "OSS releasing %d resources of type R%d for P%d at time %d:%09d\n", buf2.mresource_count, buf2.mresource_index, pcb_index, shmem->sec, shmem->nsec);
                }//fprintf(stderr,"OSS releasing %d resources of type R%d for P%d at time %d:%09d\n", buf2.mresource_count, buf2.mresource_index, pcb_index, shmem->sec, shmem->nsec);

                // Subtract the requested resource from the allocated vector for this process,
                shmem->alloc[pcb_index][buf2.mresource_index] -= buf2.mresource_count;

                // Add the # of requested resources to the available resource vector
                shmem->available[buf2.mresource_index] += buf2.mresource_count;

                // See if it is safe to unblock any processes
                for (int i = 0; i < PROC_LIMIT; i++) {
                    if (blocked[i][1] > 0) { // If this blocked process requested more than zero resources (meaning it was blocked)
                        bool safety = safety_algorithm(i, blocked[i][0], blocked[i][1]);
                        if (safety) { // If the algorithm said that this BLOCKED process was okay to be UNBLOCKED,
                          
                            if (++line_count < MAX_LINES) {
                                fprintf(fptr, "OSS unblocking P%d at time %d:%d\n", i, shmem->sec, shmem->nsec);
                            }//fprintf(stderr,"OSS unblocking P%d at time %d:%d\n", i, shmem->sec, shmem->nsec);
                            if (++line_count < MAX_LINES && verbose) {                            
                                fprintf(fptr, "OSS granting P%d request for %d resources of type R%d at time %d:%09d\n", i, blocked[i][1], blocked[i][0], shmem->sec, shmem->nsec);
                            }//fprintf(stderr,"OSS granting P%d request for %d resources of type R%d at time %d:%09d\n", i, blocked[i][1], blocked[i][0], shmem->sec, shmem->nsec);

                            // Track the number of granted requests
                            granted_requests++;

                            // Set the Resource index and count, for adding to allocated and removing from available
                            int resource_index = blocked[i][0];
                            int resource_count = blocked[i][1];
                            
                            // Add the requested resource to the allocated vector for this process,
                            shmem->alloc[i][resource_index] += resource_count;

                            // Remove the # of requested resources from the available resources vector
                            shmem->available[resource_index] -= resource_count;

                            print_matrices();

                            // When these values are zero, the process is seen as "UNBLOCKED"
                            blocked[i][0] = 0;
                            blocked[i][1] = 0;
                            
                            // Send a message to this user process saying that it was granted the request and unblocked
                            buf1.mtype = i+2;
                            buf1.msafe = safety;
                            msgsnd(mqid, &buf1, sizeof(buf1), 0); 
                        }
                    }
                }
            }
            count++;
        }
    }    
    
    printf("Simulation complete, ending!\n");

    // Close the log file if it wasn't closed already
    if (fptr) {
        fclose(fptr);
    }
    fptr = NULL;
 
    // Clear shared memory
    free_memory();
    return 0;

}

// Whenever a request is made, this function gets ran
bool check_to_block(int process_index, int resource_index, int resources) {

    // Temporarily adds this request to the blocked 2D array
    blocked[process_index][0] = resource_index; // The type of resource requested
    blocked[process_index][1] = resources; // The number of resources requested
 
    // Checks if the request was safe or not
    bool safe = safety_algorithm(process_index, resource_index, resources);
 
    // When it is safe, unblock the process
    if (safe) {
        blocked[process_index][0] = 0; // Unblocks the process
        blocked[process_index][1] = 0;
        return true;
    } // Otherwise, the process stays blocked
    else {
        return false;
    }
}

// This function calculates the "needed" matrix, which is what is left over to be requested
void max_needed_in_future() {
    for (int i = 0; i < PROC_LIMIT; i++) {
        for (int j = 0; j < RESOURCE_LIMIT; j++) {
            shmem->needed[i][j] = 0;
            shmem->needed[i][j] = shmem->claim[i][j] - shmem->alloc[i][j];
        }
    }
}

bool safety_algorithm(int process_index, int resource_index, int resources) {

    // Grabs the needed 2D array
    max_needed_in_future();
   
    // Every process starts "unfinished" (changes as this runs) 
    bool finished[PROC_LIMIT] = {0};

    // This will be the sequence of safe or unsafe processes
    int safe_sequence[PROC_LIMIT];

    // Copy for the available vector
    int copy_avail[RESOURCE_LIMIT]; 

    // Copies the available vector
    for (int i = 0; i < RESOURCE_LIMIT; i++) {
        copy_avail[i] = shmem->available[i]; 
    }

    // Perform the "requested" resources subtraction from the copy of available resources
    copy_avail[resource_index] -= resources;

    // When there aren't enough available resources, return unsafe state
    if (copy_avail[resource_index] < 0) {
        return false;
    }

    // Subtract the "requested" resources from the "needed" matrix
    shmem->needed[process_index][resource_index] -= resources;

    // count tracks every process
    int count = 0;
    // This loops until every process has been considered
    while (count < PROC_LIMIT) {
        int current_count = count;
        // Loops through each process and determines if it is safe or not
        for (int x = 0; x < PROC_LIMIT; x++) {
            if (finished[x] == 0) { // each proc starts at finished[x] == 0
                if (blocked[x][1] == 0 || x == process_index) { // Ignores blocked processes and the process_index
                    int y; // Once this hits the RESOURCE_LIMIT, there were requests
                    for (y = 0; y < RESOURCE_LIMIT; y++) { // Check that each resource of this Process
                        if (shmem->needed[x][y] > copy_avail[y]) { // is less than the available resources
                            break;
                        }
                    }
                    if (!bitv.test(x)) { // This makes sure only running processes get tested
                        safe_sequence[count++] = -1; // This process was not active or had 0 claims for every resource
                        finished[x] = 1; // It was checked, finished
                    }
                    else if (y == RESOURCE_LIMIT) { // There were requests,
                        for (int z = 0; z < RESOURCE_LIMIT; z++) { // Add allocated resources to the copy
                            copy_avail[z] += shmem->alloc[x][z]; // Add the allocated to the temp available array 
                        }
                        safe_sequence[count++] = x; // Adds this process to the safe sequence
                        finished[x] = 1; // It was checked, finished
                    }
                }
                else {
                    //fprintf(stderr,"P%d IS BLOCKED and is not THE Current PROCESS INDEX to be checked\n", x);
                    safe_sequence[count++] = -1; // Negatives are added for blocked processes (to be ignored)
                    finished[x] = 1; // Marked as "finished"
                }
            }
        }
        // The state of the system is unsafe if current_count == count
        if (current_count == count) {
            return false;
        }
    }
   
    if (++line_count < MAX_LINES && verbose) {
        //fprintf(stderr, "OSS system is in safe state, Safe Sequence: |");
        fprintf(fptr, "OSS system is in safe state, Safe Sequence: |");
        for (int i = 0; i < PROC_LIMIT; i++) {
            if (i < PROC_LIMIT-1 && safe_sequence[i] > 0) {
                //fprintf(stderr, "P%d|", safe_sequence[i]);
                fprintf(fptr, "P%d|", safe_sequence[i]);
            }
            else if (i == PROC_LIMIT && safe_sequence[i] > 0) {
                //fprintf(stderr, "P%d|\n\n", safe_sequence[i]);
                fprintf(fptr, "P%d|\n\n", safe_sequence[i]);
            }
        }
        //fprintf(stderr, "\n");
        fprintf(fptr, "\n");
    }
    return true;
}

void print_matrices() {
/*
     printf("Initial Vector:\n |");
     for (int i = 0; i < RESOURCE_LIMIT; i++) {
         printf("%2d", shmem->initial[i]);
         printf("|");
     }
     printf("\n\n");

     printf("Available Resources Vector:\n |");
     for (int i = 0; i < RESOURCE_LIMIT; i++) {
         printf("%2d", shmem->available[i]);
         printf("|");
     }
     printf("\n\n");
                 
     printf("Max Claims Matrix:\n");
     for (int i = 0; i < PROC_LIMIT; i++) {
         printf("|");
         for (int j = 0; j < RESOURCE_LIMIT; j++) {
             printf("%2d", shmem->claim[i][j]);
             printf("|");
         }
         printf("\n");
     }
     printf("\n\n");
*/

     if (++line_count < MAX_LINES && verbose) {
         fprintf(fptr, "Allocation Matrix:\n");
     }
     for (int i = 0; i < PROC_LIMIT; i++) {
         if (++line_count < MAX_LINES && verbose) {
             fprintf(fptr, "|");
             for (int j = 0; j < RESOURCE_LIMIT; j++) {
                 fprintf(fptr, "%2d", shmem->alloc[i][j]);
                 fprintf(fptr, "|");
             }
             fprintf(fptr, "\n");
         }
     }
     if (++line_count < MAX_LINES && verbose) {
         fprintf(fptr, "\n");
     }

     if (++line_count < MAX_LINES && verbose) {
         fprintf(fptr, "Blocked:\n");
     }
     for (int i = 0; i < PROC_LIMIT; i++) {
         if (++line_count < MAX_LINES && verbose) {
             fprintf(fptr, "|");
             fprintf(fptr, "%2d|", blocked[i][0]);
             fprintf(fptr, "%2d|", blocked[i][1]);
             fprintf(fptr, "\n");
         }
     }
     if (++line_count < MAX_LINES && verbose) {
         fprintf(fptr, "\n");
     }
}

// Function to increment the clock
void increment_clock() {
    int random_nsec = rand() % 1000000 + 1;
    shmem->nsec += random_nsec;
    adjust_clock();
}

// Everytime the shared memory clock gets incremented, run this to fix the values
void adjust_clock() {
    while(shmem->nsec >= 1000000000) {
        shmem->nsec -= 1000000000; // this takes care of conversions
        shmem->sec += 1;
    }
}

// Kills all child processes and terminates, and prints a log to log file and frees shared memory
void sig_handle(int signal) {
    //statistics(); // Prints statistics
    if (signal == 2) {
        printf("\n[OSS]: CTRL+C was received, interrupting process!\n"); 
    }
    if (signal == 14) { // "wake up call" for the timer being done
        printf("\n[OSS]: The countdown timer [-t time] has ended, interrupting processes!\n");
        five_second_alarm = true;
    }

    if (fptr) {
        fclose(fptr);
    }
    fptr = NULL;
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

/* The struct "sigaction" on the sigaction(2) manual page and it can be
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


// Prints a usage message about how to properly use this program
void usage(std::string name) {
    printf("\n%s: Usage: ./oss can be called using the following methods:\n\n", name.c_str());
    printf("    1. ./oss -v\n");
    printf("       this option runs in verbose mode!\n");
    printf("    2. ./oss\n");
    printf("       this option does not run in verbose mode! (default option)\n\n");
    exit(EXIT_FAILURE);
}

// Takes the shared memory struct and frees the shared memory
void free_memory() {

    // Performs control operations on the message queue
    if (msgctl(mqid, IPC_RMID, NULL) == -1) { // Removes the message queue
        perror("oss: Error: Could not remove the  message queue");
        exit(EXIT_FAILURE);
    }

    shmdt(shmem); // Detaches the shared memory of "shmem" from the address space of the calling process
    shmctl(sid, IPC_RMID, NULL); // Performs the IPC_RMID command on the shared memory segment with ID "sid"
    // IPC_RMID -- marks the segment to be destroyed. This will only occur after the last process detaches it.
    printf("[OSS]: all shared memory and message queues freed up! terminating!\n");
}
