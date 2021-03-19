/* 

	Author: Daniel Janis
	Program: Project 2 - Practice Shared Memory and IPC - CS 4760-002
    Date: 10/8/20
    File: palin.cpp
    Purpose:
	 This program, palin, is designed to be called from master, given an argument XX which
	 translates into a unique Indexing value, passed from the exec command in master.cpp. This XX argument
	 allows for keeping track of which processes are and aren't allowed into the critical section.
     Palin attaches to shared memory and then determines if a word from shared memory is a 
	 palindrome or not. After determining if the word is a palindrome or not, the multiple process
	 Peterson's Algorithm begins to check, based on the word_count-er, which process to allow into
	 the critical section to perform printing operations to outfiles and a log file. Once the process
	 that was allowed into the critical section is finished, code is executed to exit the critical section.

*/

#include <cstring>
#include <ctime>
#include <time.h>
#include <string>
#include <ctype.h>
#include "palin.h"
#include "shared.h"

int n; // Keeps track of the number of child processes to be spawned
int child_count; // Keeps track of the number of children throughout program
struct Shmem* shmem; // struct instance for shared memory
key_t skey; // Unique key for using shared memory
int sid; // Shared memory segment ID

int main(int argc, char *argv[]) {

    srand(time(NULL)); // Seed the random number generator once for the sleep() later on

    std::string error_msg; // For perror's

    // Checks if the process was killed by somebody with the *kill* program, if found, calls sig_handler()
    signal(SIGTERM, sig_handler);    

    int word_count; // Used for synchronizing the # child processes and words in the array (shared memory)
    bool is_palin = false; // used for checking palindrome or not

    if (argc != 2) { // Checks that there were two arguments (./palin XX)
        perror("palin: Error: Not enough arguments were passed, terminating");
        exit(EXIT_FAILURE);
    }
    else {
        child_count = atoi(argv[1]);
        word_count = child_count-1; // without this -1, the first line is always skipped
    }

    // Generate a (key_t) type System V IPC key for use with shared memory
    if ((skey = ftok("makefile", 'a')) == (key_t) -1) {
        perror("palin: ftok: Error: There was an error creating a shared memory key");
        exit(EXIT_FAILURE);
    }
    
    // Tries to allocate a shared memory segment, otherwise it will use the 
    // previously allocated shared memory segment
    if ((sid = shmget(skey, sizeof(struct Shmem), IPC_CREAT | 0666)) < 0) { 
        // if the sid is < 0, it couldn't allocate a shared memory segment
        perror("palin: shmget: Error: An error occurred while trying to allocate a valid shared memory segment");
        exit(EXIT_FAILURE);
    }
    else {
        shmem = (struct Shmem*) shmat(sid, NULL, 0); 
        // attaches to Sys V shared mem segment using previously allocated memory segment (sid)
        // Since shmaddr is NULL, system chooses a suitable (unused) page-aligned address to attach the segment
    }

    // Starts the word count at the right index (index 0)
    char* string = shmem->words[word_count];

    // Removes any trailing newlines and replaces with null-terminator
    for (int i = 0; string[i] != '\0'; i++) {
        if (string[i] == '\n') {
            string[i] = '\0';
        }
    }

    int length = strlen(string);
    char temp_str[length];
    strcpy(temp_str, string); // Saves the original string into temp_str

    // Temporarily remove punctuation, capitalization, and spaces from "string"
    int index = 0;
    char c;
    while(string[index]) { // Loops through every character
        c = string[index]; // Grabs a character
        string[index] = tolower(c); // Makes sure the letters are lowercase
        if (ispunct(c) != 0) // If there is punctuation,
            string[index] = ' '; // replace it with a space
        if (isspace(c) != 0) // If there is a space (or tab),
            string[index] = ' '; // replace it with a space
        index++; // Move to next character
    }
    index = 0; // Now to remove the spaces
    for (int i = 0; string[i]; i++) { // For every character in the string,
        if (string[i] != ' ') // If theres a space, skip it and its index
            string[index++] = string[i];
    }
    string[index] = '\0'; // C-strings have a null terminator at the end

    // Checks if a string is a palindrome or not, returns true or false
    is_palin = palin_check(string);
    
    strcpy(shmem->words[word_count], temp_str); // Return the original string to shared memory

    // process() handles the critical section problem
    fprintf(stderr, "[%s] executing code to enter critical section\n", get_time());
    process(word_count, is_palin);

    return 0;
}

// This function was HEAVILY inspired by the Multiple process solution
// of the Peterson's Algorithm from the book/slides
void process(const int i, bool is_palin) {

    n = shmem->n; // The number of child processes to be spawned

    int j; // Local variable for the next waiting process
    do {
        shmem->flag[i] = want_in; // Raise the flag of the current process wanting in
        j = shmem->turn; // set local variable
        while (j != i) { // this while loop executes until it is i's turn
            if (shmem->flag[j] != idle) {
                j = shmem->turn;
            }
            else {
                j = (j + 1) % n;
            }
        }
        shmem->flag[i] = in_cs; // Declare intention to enter critical section (i's turn)
        // only ONE process can set the flag to "in_cs" at a time

        for (j = 0; j < n; j++) { // Check that no one else is in the critical section
            if ((j != i) && (shmem->flag[j] == in_cs))
                break;
        }
    } while ((j < n) || (shmem->turn != i && shmem->flag[shmem->turn] != idle));
    /* while j is less than the number of child processes to be spawned, 
                               or
       it isn't i's turn AND the j's flag is not idle (want_in or in_cs) */

    shmem->turn = i; // Assign turn to self and enter critical section

        /* NOW ENTERING CRITICAL SECTION */

    critical_section(i, is_palin); // (i was word_count, is its string a palindrome or not)

        /* EXECUTE CODE TO EXIT FROM CRITICAL SECTION */

    j = (shmem->turn + 1) % n;
    while (shmem->flag[j] == idle) // Exit section
        j = (j + 1) % n; 
    shmem->turn = j; // Assign turn to the next waiting process
    shmem->flag[i] = idle; // Lower flag of the current process
}

// Prints logs, prints the palindromes to the correct out-files
void critical_section(int word_count, bool is_palin) {
    fprintf(stderr, "[%s] ENTERED - Critical Section\n", get_time());
    int random = (rand() % 3) + 0; // Sets random to a random integer 0-2
    sleep(random); // Sleeps for a random amount of time (0 to 2 seconds)
    if (is_palin) { // If it is a palindrome, print the word to palin.out
        FILE *palin = fopen("palin.out", "a");
        if (palin == NULL) { // make sure the file opens
            perror("palin: Error: Cannot open file \"palin.out\" for output, terminating");
            exit(EXIT_FAILURE);
        }
        printf("\nPALIN! -> %s\n\n", shmem->words[word_count]);
        fprintf(palin, "%s\n", shmem->words[word_count]);
        fclose(palin); // close the file
    }
    else { // If it is not a palindrome, print the word to nopalin.out
        FILE *nopalin = fopen("nopalin.out", "a");
        if (nopalin == NULL) { // make sure the file opens
            perror("palin: Error: Cannot open file \"nopalin.out\" for output, terminating");
            exit(EXIT_FAILURE);
        }   
        printf("\nnonPALIN! -> %s\n\n", shmem->words[word_count]);
        fprintf(nopalin, "%s\n", shmem->words[word_count]);
        fclose(nopalin); // close the file
    }

    FILE *logfile = fopen("output.log", "a+");
    if (logfile == NULL) { // make sure the logfile opens
        perror("palin: Error: Cannot open log file for output, terminating");
        exit(1);
    } // Prints the time, PID, Index, and the String to the log file
    fprintf(logfile, "[%s], %d, %d, %s\n", get_time(), getpid(), word_count+1, shmem->words[word_count]);
    fclose(logfile); // close the log file

    fprintf(stderr, "[%s] EXITED - Critical Section\n", get_time());
} // Leaving the critical section

void sig_handler(int signal) {
    if (signal == 15) {
        printf("[palin]: kill signal has been received, interrupting process!\n");
        exit(EXIT_FAILURE);
    }
}

// This function returns true if the given char* is a palindrome, false if not
bool palin_check(char* temp) {
    int size = strlen(temp);
    char* backward = new char[size];
    strcpy(backward, temp); 
    backward = strrev(backward); // Reverses the string to be compared below
    if (strcmp(temp, backward) ==  0) {
        return true;
    }
    else {
        return false;
    }
}

// This function reverses and returns the reversed string
char *strrev(char* string) {
    if (!string || ! *string) // Check that it is a string
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

// This function gets the local time at the time of calling
char *get_time() {
    time_t seconds = time(0); // Holds the current time from system
    struct tm * formatted = localtime(&seconds); // gives local time representation of timer
    char* ch_time = new char[12]; // Allocates new memory for a char ptr array
    strftime(ch_time, 12, "%I:%M:%S %p", formatted); // Store the formatted time
    return (char*) ch_time; // Returns a formatted time to be printed
}
