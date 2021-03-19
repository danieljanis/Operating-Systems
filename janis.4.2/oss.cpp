/*
 
	Author: Daniel Janis
	Program: Project 4 - Process Scheduling - CS 4760-002
	Date: 11/5/20
    File: oss.cpp
	Purpose: Main program

 This program serves as the master process in an Simulated OS Scheduler.
 When this simulator starts running it will fork multiple children at random times.
 The randomness of these child processes being forked is created with the use of
 a clock which is stored in shared memory ("shmem") with seconds and nanoseconds.
 
 Inside of "shmem", a Process Table ("Pcb procs" inside of shared.h) of size 18 is
 created. Only 20 processes are allowed to be running at once, so we can only spawn 18
 children. Each of these indices contain a Process Control Block (struct Pcb in shared.h)
 which stores critical timing information about the current child processes in the system.
 The "Pcb" contains total CPU time, total time in the system, time used during the last
 burst, the local simulated pid and the process priority.

 Local to oss.cpp, a bitset named "bitv" of size 18 is created to help keep track of the
 process control blocks (simulated_PID's) that are already taken (1 if taken, 0 if not)

 This program will generate user processes at random intervals, based on the values 
 "MAX_TIME_SE"C and "MAX_TIME_NSEC" in shared.h

 Time is simulated in this system by incrementing the shared clock. If a child ./user_proc
 uses some time, this clock should be advanced to indicate the used time. If ./oss does 
 something that would take some time in a real OS, the clock should be advanced to indicate
 a small amount of time spent by ./oss.

 Once the random time to generate a new process has elapsed, this program will initialize
 the Pcb for the new process, and then fork the process. The child process wil execl 
 ./user_proc and this will happen about once every ~randomized~ second!

 This program controls concurrency for every ./user_proc. The main while() loop on line ###
 is where processes get set up, time is generated to launch a new process and then using
 the message queue (buf1) a process gets scheduled to run by sending a message! After sending
 that message, it waits to receive a message (buf2) which will contain information like:
 did the program get INTERRUPTED, or TERMINATE, or RUN FOR ITS FULL QUANTUM, along with the
 time that it ran in nanoseconds. If the process table is full (bitv is full of 1's), then
 a new random time to try to generate a new process is determinted. Every time the while() 
 loop actually loops, the clock gets advanced using the function "dispatcher_does_work()" 
 which simulates overhead activity in the system.

 SCHEDULING ALGORITHM (Multi-level Feedback Queue):
 Assuming there is more than one process in the system, a process from the highest priority
 queue at the very front of the queue is selected (unless all queues are empty) and it is
 scheduled to execute. There are 4 queues, queue1 is the highest priority and queue4 is the
 lowest. "queue1" has a quantum of QUANT_1 (from shared.h) and the quantum doubles every time
 a process gets moved down a level in the queues. 
    1. when a process used its entire quantum, move it down 1 priority queue
    2. if a process leaves a blocked queue (determined by a random time from 0-3 seconds and
       0-1000 nanoseconds PLUS the current time that is calculated whenever the process
       gets blocked) it gets moved to the highest priority queue (queue1)
 Once the process has been selected, it gets dispatched by sending the process a message (buf1)
 indicating how much of a quantum it has to run. Since this scheduling takes time, before launching
 the process ./oss should increment the clock for the amount of work that it did (100-1000 nanoseconds)

*/

#include <sys/wait.h>
#include <sys/time.h>
#include <string.h>
#include <bitset>
#include <vector>
#include <algorithm>
#include <iostream>
#include "oss.h"
#include "shared.h"

// My queues!
std::queue<int> temp;
std::queue<int> queue1;
std::queue<int> queue2;
std::queue<int> queue3;
std::queue<int> queue4;
std::vector<int> blocked;

// For the average blocked wait time
struct Blocked_Time block_time;
std::vector<Blocked_Time> wait_times;

// For indexing, printing!
static int pcb_index = 0;
static int simulated_PID = 0;
static int proc_count = 0;

// For statistics
static int total_CPU_time_used_s = 0;
static int total_CPU_time_used_ns = 0;
static int total_number_of_bursts = 0;
static int total_CPU_idle_s = 0;
static int total_CPU_idle_ns = 0;

// Used to print a log with statistics at the very end
static std::string logfile = "logfile.log";

// Shared memory
struct Shmem* shmem;
// Message queues
struct Msgbuf buf1, buf2;
key_t skey; // shared memory key
int sid; // shared memory id
key_t mkey; // message queue key
int mqid; // message queue id

int main(int argc, char *argv[]) {

    // TO TRACK/TRUNCATE THE LOGFILE's LENGTH
    int line_count = 0;

    int dispatcher = 0;
    
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
    
    if (argc > 1) {
        fprintf(stderr, "%s: Error: Too many arguments when running the simulator!\n", exe_name.c_str());
        usage(exe_name.c_str());
    }

    // OPEN LOG FILE FOR WRITING
    FILE *fptr;
    fptr = fopen(logfile.c_str(), "w+");

    // bitv KEEPS TRACK OF WHICH Pcb's ARE IN USE
    std::bitset<PROC_LIMIT> bitv;

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
    int terminate_after = 3; // real life seconds to run the simulation
    countdown_to_interrupt(terminate_after, exe_name.c_str());

    // INITIALIZE THE SHARED MEMORY CLOCK
    shmem->sec = 0;
    shmem->nsec = 0;
    shmem->shmPID = 0;

    // Generate a random amount of time from 1 nanosecond to 2 seconds
    unsigned int tsec = (rand() % MAX_TIME_SEC) + 0;
    unsigned int tnsec = (rand() % MAX_TIME_NSEC) + 1;
    if (tnsec == 1000000000) {
        tnsec -= 1000000000;
        tsec += 1;
    }
 
    // When this is true, there are no Process Control Blocks free in the bitv
    bool resumed = false;

    // Jumps to the time where the first process should be scheduled (from random values)
    if (proc_count == 0) {
        shmem->sec += tsec;
        shmem->nsec += tnsec;
    }
    adjust_clock();
    
    // This is where the magic happens
    while(1) {
        // CHECK TIME FOR IF A PROCESS CAN BE CREATED YET
        if (shmem->sec == tsec && shmem->nsec >= tnsec || shmem->sec > tsec) {
            if (proc_count > 99) {
                printf("OSS: 100 processes have terminated!\n");
                sig_handle(1); // Sends a signal that 100 processes were generated, exiting
                break; // Stops everything
            }

            // Loop through bitv and find FIRST AVAILABLE PID (index used in the Process Table)
            pcb_index = -1;
            simulated_PID = -1;
            for (int i = 0; i < bitv.size(); i++) {
                if (!bitv.test(i)) { // Locates first available PID ( a zero )
                    pcb_index = i; // Grabs smallest index, available PID (index with Pcb procs)
                    simulated_PID = i+2; // This is our simulated PID in the system (printable)
                    bitv.set(i); // SETS BITVECTOR THAT WE ARE USING THIS PID NOW
                    resumed = false; // theres room for a new process
                    break;
                }
            }
            if (simulated_PID == -1) {
                resumed = true; // The bitv is full, cannot create a process yet
            }
        
            if (pcb_index >= 0 && !resumed) {
            
                // initialize shared memory PCB
                shmem->procs[pcb_index].simPID = simulated_PID;
                shmem->procs[pcb_index].priority = 1; // Always start with the highest queue
                shmem->procs[pcb_index].total_s = 0;   // Total time in system (sec)
                shmem->procs[pcb_index].total_ns = 0; // (nanosec)
                shmem->procs[pcb_index].CPU_s = 0;              // Time on CPU (sec)
                shmem->procs[pcb_index].CPU_ns = 0;             // (nanosec)
                shmem->procs[pcb_index].burst_s = 0;         // Time of the last burst (sec)
                shmem->procs[pcb_index].burst_ns = 0;       // (nanosec)
                shmem->procs[pcb_index].resume_s = -1;  // When to unblock a process
                shmem->procs[pcb_index].resume_ns = -1; // (nanosec)

                // For sending the simulated PID through execl (used by ./user_proc to receive messages)
                char count[3];
                snprintf(count, 3, "%d", simulated_PID);
                
                // Fork child process
                childpid = fork();
                if (childpid == 0) {
                    shmem->pgid = getpid();
                    execl("./user_proc", "user_proc", count, '\0');
                    error_msg = exe_name + ": Error: Failed to execl";
                    perror(error_msg.c_str());
                    exit(EXIT_FAILURE);
                }
                if (childpid < 0) { // Check that the child forked successfully
                    error_msg = exe_name + ": Error: Failed to fork a child";
                    perror(error_msg.c_str());
                    exit(EXIT_FAILURE);
                }

                if (++line_count < MAX_LINES) {
                    fprintf(fptr, "OSS: Generating process with PID %d and putting it in queue %d at time %d:%09d\n", simulated_PID, shmem->procs[pcb_index].priority, shmem->sec, shmem->nsec);
                } //printf("OSS: Generating process with PID %d and putting it in queue %d at time %d:%09d\n", simulated_PID, shmem->procs[pcb_index].priority, shmem->sec, shmem->nsec);

                // Start the process in the highest queue
                queue1.push(pcb_index);

                // Once this hits 100, end simulation
                proc_count += 1;
            }
            // Get new random time for a process to be generated
            tsec = (rand() % MAX_TIME_SEC) + 0 + shmem->sec;
            tnsec = (rand() % MAX_TIME_NSEC) + 1 + shmem->nsec;
            if (tnsec == 1000000000) {
                tnsec -= 1000000000;
                tsec += 1;
            }
        }  

        // This loop checks if any blocked processes have passed the necessary time to wake up (resume)
        for (int i = 0; i < blocked.size(); i++) {
            bool sec_check = shmem->sec > shmem->procs[blocked.at(i)].resume_s;
            bool nanosec_check = shmem->sec == shmem->procs[blocked.at(i)].resume_s && shmem->nsec >= shmem->procs[blocked.at(i)].resume_ns;
            if (nanosec_check || sec_check) {
                pcb_index = blocked.at(i); // Grabs the index of the process to be unblocked
                std::vector<int>::iterator i = std::find(blocked.begin(), blocked.end(), pcb_index); // Unblocks the process
                blocked.erase(i); // Removes the process from the vector

                if (++line_count < MAX_LINES) {
                    fprintf(fptr, "OSS: Removing process with PID %d from blocked queue\n", pcb_index+2);
                } //printf("OSS: Removing process with PID %d from blocked queue\n", pcb_index+2);

                buf1.priority = 1; // Puts the process in the highest queue
                shmem->procs[pcb_index].priority = buf1.priority;

                if (++line_count < MAX_LINES) {
                    fprintf(fptr, "OSS: Putting process with PID %d into queue %d\n", pcb_index+2, shmem->procs[pcb_index].priority);
                } //printf("OSS: Putting process with PID %d into queue %d\n", pcb_index+2, shmem->procs[pcb_index].priority);

                queue1.push(pcb_index); // Resumes the process into the highest priority queue
                break;
            }
        }
    
        // LOOKS FOR THE FIRST AVAILABLE, HIGHEST PRIORITY PROCESS IN THE QUEUES AND SCHEDULES THAT PROCESS
        if (!queue1.empty()) {
            pcb_index = queue1.front();
            queue1.pop();
            buf1.priority = shmem->procs[pcb_index].priority;
            if (++line_count < MAX_LINES) {
                fprintf(fptr, "OSS: Removing process with PID %d from queue %d\n", pcb_index+2, shmem->procs[pcb_index].priority);
            } //printf("OSS: Removing process with PID %d from queue %d\n", pcb_index+2, shmem->procs[pcb_index].priority);
        }
        else if (!queue2.empty()) {
            pcb_index = queue2.front();
            queue2.pop();
            buf1.priority = shmem->procs[pcb_index].priority;
            if (++line_count < MAX_LINES) {
                fprintf(fptr, "OSS: Removing process with PID %d from queue %d\n", pcb_index+2, shmem->procs[pcb_index].priority);
            } //printf("OSS: Removing process with PID %d from queue %d\n", pcb_index+2, shmem->procs[pcb_index].priority);
        }
        else if (!queue3.empty()) {
            pcb_index = queue3.front();
            queue3.pop();
            buf1.priority = shmem->procs[pcb_index].priority;
            if (++line_count < MAX_LINES) {
                fprintf(fptr, "OSS: Removing process with PID %d from queue %d\n", pcb_index+2, shmem->procs[pcb_index].priority);
            } //printf("OSS: Removing process with PID %d from queue %d\n", pcb_index+2, shmem->procs[pcb_index].priority);
        }
        else if (!queue4.empty()) {
            pcb_index = queue4.front();
            queue4.pop();
            buf1.priority = shmem->procs[pcb_index].priority;
            if (++line_count < MAX_LINES) {
                fprintf(fptr, "OSS: Removing process with PID %d from queue %d\n", pcb_index+2, shmem->procs[pcb_index].priority);
            } //printf("OSS: Removing process with PID %d from queue %d\n", pcb_index+2, shmem->procs[pcb_index].priority);
        }
        else if (queue1.empty() && queue2.empty() && queue3.empty() && queue4.empty()) { // IF ALL QUEUES ARE EMPTY,  
            // Difference between current time and the time to generate a new process
            int idle_time_ns = tnsec - shmem->nsec;
            int idle_time_s = tsec - shmem->sec;
            if (idle_time_ns < 0) { 
                idle_time_ns += 1000000000;
                idle_time_s -= 1;
            }

            // For keeping statistics
            total_CPU_idle_s += idle_time_s;
            total_CPU_idle_ns += idle_time_ns;
            while(total_CPU_idle_ns >= 1000000000) {
                total_CPU_idle_ns -= 1000000000;
                total_CPU_idle_s += 1;
            }
      
            // Jump to generate the next process
            shmem->sec += idle_time_s;
            shmem->nsec += idle_time_ns;
            adjust_clock();
            continue;
        }
        simulated_PID = pcb_index+2; // Fixes the indexing on simulated_PID

        switch (buf1.priority) { // Sets the timeslices each priority level is allowed to run
            case 1:
                buf1.priority = QUANT_1;
                break;
            case 2:
                buf1.priority = QUANT_2;
                break;
            case 3:
                buf1.priority = QUANT_3;
                break;
            case 4:
                buf1.priority = QUANT_4;
                break;
        }

        // Add time to shared memory clock to represent the time it takes to schedule the next process
        unsigned int dispatcher = 0;
        dispatcher = dispatcher_does_work();
        if (++line_count < MAX_LINES) {
            fprintf(fptr, "OSS: Dispatching process with PID %d from queue %d at time %d:%09d\n", simulated_PID, shmem->procs[pcb_index].priority, shmem->sec, shmem->nsec);
        } //printf("OSS: Dispatching process with PID %d from queue %d at time %d:%09d\n", simulated_PID, shmem->procs[pcb_index].priority, shmem->sec, shmem->nsec);
        if (++line_count < MAX_LINES) {
            fprintf(fptr, "OSS: total time this dispatch was %d nanoseconds\n", dispatcher);
        } //printf("OSS: total time this dispatch was %d nanoseconds\n", dispatcher);
    
        // SEND MESSAGE with simulated_PID, allowing process to run
        buf1.mtype = simulated_PID;
        buf1.mflag = -1; // unneccessary flag for this first message to the user
        if (msgsnd(mqid, &buf1, sizeof(buf1), 0) < 0) { // SEND a message from OSS to USER, enter the critical region
            error_msg = exe_name + ": Error: msgsnd: the message did not send";
            perror(error_msg.c_str());
            free_memory();
            exit(EXIT_FAILURE);
        }

        // RECEIVE MESSAGE
        if (msgrcv(mqid, &buf2, sizeof(buf2), 1, 0) < 0) {
            error_msg = exe_name + ": Error: msgrcv: the message was not received";
            perror(error_msg.c_str()); 
            free_memory();
            exit(EXIT_FAILURE);
        }
        if (++line_count < MAX_LINES) {
            fprintf(fptr, "OSS: Receiving that process with PID %d that ran for %d nanoseconds\n", simulated_PID, buf2.priority);
        } //printf("OSS: Receiving that process with PID %d that ran for %d nanoseconds\n", simulated_PID, buf2.priority);
    

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // Get how long the child ran for
        shmem->procs[pcb_index].burst_ns = buf2.priority;

        // Adjust seconds and nanoseconds
        while (shmem->procs[pcb_index].burst_ns >= 1000000000) {
            shmem->procs[pcb_index].burst_ns -= 1000000000;
            shmem->procs[pcb_index].burst_s += 1;
        }

        total_CPU_time_used_s += shmem->procs[pcb_index].burst_s;
        total_CPU_time_used_ns += shmem->procs[pcb_index].burst_ns;
        while (total_CPU_time_used_ns >= 1000000000) {
            total_CPU_time_used_ns -= 1000000000;
            total_CPU_time_used_s += 1;
        }
        total_number_of_bursts += 1; // A burst has been completed

        // Add how long the last process ran on the clock
        shmem->sec += shmem->procs[pcb_index].burst_s;
        shmem->nsec += shmem->procs[pcb_index].burst_ns;
        adjust_clock();
 
        // Add burst time to the CPU clock
        shmem->procs[pcb_index].CPU_ns += buf2.priority;

        // Adjust seconds and nanoseconds
        while(shmem->procs[pcb_index].CPU_ns >= 1000000000) {
            shmem->procs[pcb_index].CPU_ns -= 1000000000;
            shmem->procs[pcb_index].CPU_s += 1;
        }

        // Time the process has been in the system (even while others are running)
        shmem->procs[pcb_index].total_ns += buf2.priority;

        // Adjust seconds and nanoseconds
        while(shmem->procs[pcb_index].total_ns >= 1000000000) { 
            shmem->procs[pcb_index].total_ns -= 1000000000;
            shmem->procs[pcb_index].total_s += 1;
        }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
 
        if (buf2.mflag == 3) { // TERMINATED
            if (++line_count < MAX_LINES) {
                fprintf(fptr, "OSS: Process with PID %d did not use its full time quantum\n", simulated_PID);
            } //printf("OSS: Process with PID %d did not use its full time quantum\n", simulated_PID);
            waitpid(shmem->shmPID, NULL, 0);
            bitv.reset(simulated_PID-2);
            if (++line_count < MAX_LINES) {
                fprintf(fptr, "OSS: Process with PID %d terminated at time %d:%09d\n", simulated_PID, shmem->sec, shmem->nsec);
            } //printf("OSS: Process with PID %d terminated at time %d:%09d\n", simulated_PID, shmem->sec, shmem->nsec);
        } 
        else if (buf2.mflag == 2) { // USED ALL ITS QUANTUM

            if (buf2.priority == 10000000 || buf2.priority == 20000000 || buf2.priority == 30000000 || buf2.priority == 40000000) {
                if (++line_count < MAX_LINES) {
                    fprintf(fptr, "OSS: Process with PID %d used its full time quantum\n", simulated_PID);
                } //printf("OSS: Process with PID %d used its full time quantum (%d nanoseconds)\n", simulated_PID, buf2.priority);
            }

            // Move down 1 priority queue
            if (shmem->procs[pcb_index].priority < 4) {
                shmem->procs[pcb_index].priority += 1;
            }
            if (shmem->procs[pcb_index].priority == 1) {
                queue1.push(pcb_index);
            }
            else if (shmem->procs[pcb_index].priority == 2) {
                queue2.push(pcb_index);
            }
            else if (shmem->procs[pcb_index].priority == 3) {
                queue3.push(pcb_index);
            }
            else if (shmem->procs[pcb_index].priority == 4) {
                queue4.push(pcb_index);
            }
            if (++line_count < MAX_LINES) { 
                fprintf(fptr, "OSS: Putting process with PID %d into queue %d\n", simulated_PID, shmem->procs[pcb_index].priority);
            } //printf("OSS: Putting process with PID %d into queue %d\n", simulated_PID, shmem->procs[pcb_index].priority);
        }
        else if (buf2.mflag == 1) { // INTERRUPTED
            // Generate random time until this blocked process resumes
            int r = rand() % 3 + 0;
            int s = rand() % 1000 + 0;

            block_time.r = r;
            block_time.s = s;
            wait_times.push_back(block_time); // Stores the wait time in a vector for easy calculation of the avg wait time (in the statistics function)

            int resume_s = shmem->sec + r;
            int resume_ns = shmem->nsec + s;
            while (resume_ns >= 1000000000) {
                resume_ns -= 1000000000;
                resume_s += 1;
            }
            // When to resume this process
            shmem->procs[pcb_index].resume_s = resume_s;
            shmem->procs[pcb_index].resume_ns = resume_ns;
            
            if (++line_count < MAX_LINES) {
                fprintf(fptr, "OSS: Process with PID %d did not use its full time quantum\n", simulated_PID);
            } //printf("OSS: Process with PID %d did not use its full time quantum\n", simulated_PID);
            // remove from current queue
            if (++line_count < MAX_LINES) {
                fprintf(fptr, "OSS: Putting process with PID %d into the blocked queue\n", simulated_PID);
            } //printf("OSS: Putting process with PID %d into the blocked queue\n", simulated_PID);

            // put into blocked queue
            blocked.push_back(pcb_index);
            if (++line_count < MAX_LINES) {
                fprintf(fptr, "OSS: Process with PID %d will resume when the time hits %d:%09d\n", simulated_PID, shmem->procs[pcb_index].resume_s, shmem->procs[pcb_index].resume_ns);
            } //printf("OSS: Process with PID %d will resume when the time hits %d:%09d\n", simulated_PID, shmem->procs[pcb_index].resume_s, shmem->procs[pcb_index].resume_ns);
        }
/*
        printf("\n");
        
        temp = queue1;
        printf("queue1: ");
        for (int i = 0; i < queue1.size(); i++) {
            printf("%d ", temp.front()+2);
            temp.pop();
        }
        printf("\n");

        temp = queue2;
        printf("queue2: ");
        for (int i = 0; i < queue2.size(); i++) {
            printf("%d ", temp.front()+2);
            temp.pop();
        }
        printf("\n");

        temp = queue3;
        printf("queue3: ");
        for (int i = 0; i < queue3.size(); i++) {
            printf("%d ", temp.front()+2);
            temp.pop();
        }
        printf("\n");

        temp = queue4;
        printf("queue4: ");
        for (int i = 0; i < queue4.size(); i++) {
            printf("%d ", temp.front()+2);
            temp.pop();
        }
        printf("\n");

        printf("blocked: ");
        for (int i = 0; i < blocked.size(); i++) {
            printf("%d ", blocked.at(i)+2);
        }
        printf("\n\n");
*/
    }
    printf("Simulation complete, printing statistics and ending!\n");

    // Calculate the average CPU utilization (Time spent in child vs. time on clock at this calculation)
    statistics(); 

    if (fptr) {
        fclose(fptr);
    }
    fptr = NULL;
    free_memory(); // Clears all shared memory
    return 0;

}

// Function to increment the clock
int dispatcher_does_work() {
    int dispatcher_ns = (rand() & 10000) + 100;
    shmem->nsec += dispatcher_ns;
    adjust_clock();
    return dispatcher_ns;
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
    statistics(); // Prints statistics
    if (signal == 2) {
        printf("\n[OSS]: CTRL+C was received, interrupting process!\n"); 
    }
    if (signal == 14) { // "wake up call" for the timer being done
        printf("\n[OSS]: The countdown timer [-t time] has ended, interrupting processes!\n");
    }

    if (signal == 1) {
        printf("\n[OSS]: 100 Processes have been generated!\n");
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
    printf("\n%s: Usage: ./oss\n", name.c_str());
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

void statistics() {
    FILE *fptr;
    fptr = fopen(logfile.c_str(), "a");

    // Calculate the CPU Utilization
    double temp1 = total_CPU_time_used_s + (total_CPU_time_used_ns / 1000000000.0);
    double temp2 = shmem->sec + (shmem->nsec / 1000000000.0);
    double CPU_util = (temp1 / temp2)*100; // Gets the percentage of the total time that children processes ran on the core
    fprintf(fptr, "\n\nCPU utilization: %f%%", CPU_util);
    printf("\n\nCPU utilization: %f%%", CPU_util);

    // Calculate the average wait time (total system time - total CPU time, divided by total processes)
    double avg_wait_general = (temp2 - temp1) / proc_count;
    fprintf(fptr, "\nAverage wait time: %f", avg_wait_general);
    printf("\nAverage wait time: %f seconds", avg_wait_general);

    // Calculate the average wait time for blocked processes
    int avg_wait_time_s = 0;
    int avg_wait_time_ns = 0;
    for (int i = 0; i < wait_times.size(); i++) {
        block_time = wait_times.at(i);
        avg_wait_time_s += block_time.r;
        avg_wait_time_ns += block_time.s;
        while(avg_wait_time_ns >= 1000000000) {
            avg_wait_time_ns -= 1000000000;
            avg_wait_time_s += 1;
        }
    }
    double avg_wait_time = (avg_wait_time_s + (avg_wait_time_ns / 1000000000.0)) / wait_times.size();
    fprintf(fptr, "\nAverage time a process waited in a blocked queue: %f seconds", avg_wait_time);
    printf("\nAverage time a process waited in a blocked queue: %f seconds", avg_wait_time);

    // Print the Total Idle time in the system
    fprintf(fptr, "\nTotal CPU idle time: %d:%09d (seconds:nanoseconds)", total_CPU_idle_s, total_CPU_idle_ns);
    printf("\nTotal CPU idle time: %d:%09d (seconds:nanoseconds)", total_CPU_idle_s, total_CPU_idle_ns);

    fprintf(fptr, "\nTime on the shared memory clock when the system terminates: %d:%09d (seconds:nanoseconds)\n", shmem->sec, shmem->nsec);
    printf("\nTime on the shared memory clock when the system terminates: %d:%09d (seconds:nanoseconds)\n", shmem->sec, shmem->nsec);
    if (fptr) {
        fclose(fptr);
    }
    fptr = NULL;
}
