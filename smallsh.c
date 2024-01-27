#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

#define MAXCOM 2048 // Max number of characters supported
#define MAXARG 512 // Max number of arguments supported

int bg = 1; // Allowing background processes by &
int pid_count = 0;
pid_t pid_arr[100];
int sigtstpFlag;
int childStatus = 0;

/* Reading the command line */
char* read_line() {

    int pos = 0;
    char* buffer = malloc(sizeof(char) * MAXCOM);
    int character;

    // Loop to read each character until the end of line
    while (1) {
        // Get character
        character = getchar();

        // If we get comment, replace end with null character and return buffer as string
        if (character == '\n') {
            buffer[pos] = '\0';
            return buffer;
        }
        else {
            buffer[pos] = character;
        }
        pos++;
    }
}

/* Executing the command line */
int exec_line(char* arg[], char* input_file, char* output_file, int* bg_process, struct sigaction sa) {

    int i, j;

    // exit, cd, status commands
    if (!strcmp(arg[0], "exit")) {

        // Terminate all background processes
        int i;
        for (i = 0; pid_arr[i] != NULL; i++) {
            kill(pid_arr[i], SIGKILL);
        }

        exit(0);

    }
    else if (!strcmp(arg[0], "cd")) {

        if (arg[1] == NULL) {
            chdir(getenv("HOME"));
        }
        else {
            chdir(arg[1]);
        }

    }
    else if (!strcmp(arg[0], "status")) {
        if (bg_process == 1) {
            bg_process = 0;
        }

        printStatus();

    } else {

        // Check if background mode is ignored
        if (bg == 0){
            bg_process = 0;
        }
        
        // Other commands, fork a new process
        pid_t spawnpid = fork();

        // Append new child process in background to array
        pid_arr[pid_count] = spawnpid;
        // Count number of child processes in background
        pid_count++;

        switch (spawnpid) {
        case -1:
            perror("fork() failed!");
            exit(1);
            break;

        case 0:
            // Applying ^C to child
            if (bg_process == 0) {
                sa.sa_handler = SIG_DFL;
                sigaction(SIGINT, &sa, NULL);
            }


            // Handle input redirection
            if (input_file != NULL) {

                // bg process is not redirected
                if (bg_process == 1) {
                    input_file = "/dev/null";
                }
                // open
                int input = open(input_file, O_RDONLY);
                if (input == -1) {
                    printf("Error could not open file\n");
                    fflush(stdout);
                    exit(1);
                }
                // redirect
                int in_result = dup2(input, STDIN_FILENO);
                if (in_result == -1) {
                    printf("Error could not open file\n");
                    fflush(stdout);
                    exit(1);
                }
                // close
                fcntl(input, F_SETFD, FD_CLOEXEC);

            }

            // Handle output redirection
            if (output_file != NULL) {

                // bg process is not redirected
                if (bg_process == 1) {
                    output_file = "/dev/null";
                }
                // open
                int output = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (output == -1) {
                    printf("Error could not open file\n");
                    fflush(stdout);
                    exit(1);
                }
                // redirect
                int out_result = dup2(output, STDOUT_FILENO);
                if (out_result == -1) {
                    printf("Error could not open file\n");
                    fflush(stdout);
                    exit(1);
                }
                // close
                fcntl(output, F_SETFD, FD_CLOEXEC);

            }

            // Child Process
            // Replace the current program with new command
            if (execvp(arg[0], arg) == -1) {
                // Exec only returns if there is an error
                printf("No such file or directory\n");
                fflush(stdout);
                exit(1);
            }
            break;

        default:
            // Parent Process
            if (bg_process == 1) {

                printf("background pid is %d\n", spawnpid);
                fflush(stdout);

            }
            else {

                //wait for child to terminate
                waitpid(spawnpid, &childStatus, 0);
                if (sigtstpFlag != 1) {
                    //check that child is dead
                    if (WIFSIGNALED(childStatus) == 1 && WTERMSIG(childStatus) != 0) {
                        printf("terminated by signal %d\n", WTERMSIG(childStatus));
                    }
                    while (spawnpid != -1) {
                        //fetch pid
                        spawnpid = waitpid(-1, &childStatus, WNOHANG);
                        //print after results killed
                        if (WIFEXITED(childStatus) != 0 && spawnpid > 0) {
                            printf("background pid %d is done: exit value %d\n", spawnpid, WEXITSTATUS(childStatus));
                        }
                        else if (WIFSIGNALED(childStatus) != 0 && spawnpid > 0 && bg == 0) {
                            printf("background pid %d is done: terminated by signal %d\n", spawnpid, WTERMSIG(childStatus));
                        } 
                        // break if not waiting for child to terminate
                        else if (spawnpid == 0){
                            break;
                        }
                    }
                }

            }

        }
    
    
    }


}

/* Signal handler for SIGTSTP */
void handler_SIGTSTP(int signo) {

    // Enters foreground mode    
    if (bg == 1) {
        char* msg = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, msg, 49);
        fflush(stdout);
        bg = 0;
    }
    else {

        // Exits foreground mode
        char* msg = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, msg, 29);
        fflush(stdout);
        bg = 1;
    }
    sigtstpFlag = 1;
}

/* Printing exit status */
void printStatus() {
    // Normal exit value
    if (WIFEXITED(childStatus)) {
        printf("exit value %d\n", WEXITSTATUS(childStatus));

    }
    else if (WIFSIGNALED(childStatus)) {
        printf("terminated by signal %d\n", WTERMSIG(childStatus));
    }

}


int main() {

    char* line;
    char* input = NULL;
    char* output = NULL;
    int bg_process = 0;
    char* arg[MAXARG] = { NULL };

    // Signal Handlers

    // Ignore Ctrl+C
    struct sigaction SIGINT_action = {{ 0 }};

    // Fill out the SIGINT_action struct
    // Use SIG_IGN for the signal handler to ignore
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);

    // No flags set
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Redirect Ctrl+Z
    struct sigaction SIGTSTP_action = {{ 0 }};

    // Use handler_SIGTSTP for the signal handler
    SIGTSTP_action.sa_handler = handler_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);


    //Create command prompt loop
    while (1) {
        // Reset variable
        sigtstpFlag = 0;
        bg_process = 0;

        // Command prompt
        printf(": ");
        fflush(stdout);
        line = read_line();

        // Expansion of Variable $$
        while (strstr(line, "$$")) {

            char* line2 = strdup(line);
            int i = 0;
            while (line[i] != '\0') {
                if (line[i] == '$' && line[i + 1] == '$') {
                    line[i] = '%';
                    line[i + 1] = 'd';
                }
                i++;
                // Replace indexed items with pid
                sprintf(line2, line, getpid());
                strcpy(line, line2);

            }
            free(line2);
        }

        // Ignore blank and comment entries
        if (line[0] == '\0' || line[0] == '#' || line[0] == '\n') {
            continue;

        }
        else {

            // Strip " \n" from the command line
            char* token = strtok(line, " \n");

            // Check if < or > or & exists
            int i;
            for (i = 0; token; i++) {

                // Background process
                if (!strcmp(token, "&")) {
                    bg_process = 1;
                }

                // Input file
                else if (!strcmp(token, "<")) {
                    if (token != NULL) {
                        token = strtok(NULL, " ");
                        input = calloc(strlen(token) + 1, sizeof(char));
                        strcpy(input, token);

                    }
                    else {
                        input = calloc(10, sizeof(char));
                        strcpy(input, "/dev/null");
                    }

                }

                // Output file
                else if (!strcmp(token, ">")) {
                    if (token != NULL) {
                        token = strtok(NULL, " ");
                        output = calloc(strlen(token) + 1, sizeof(char));
                        strcpy(output, token);

                    }
                    else {
                        output = calloc(10, sizeof(char));
                        strcpy(output, "/dev/null");
                    }
                }

                // Arguments
                else {
                    arg[i] = strdup(token);
                }

                // Next token
                token = strtok(NULL, " ");

            }
           
            // Execute other commands
            exec_line(arg, input, output, bg_process, SIGINT_action);
            
        }

        free(line);
        free(input);
        free(output);

        // clear variables
        int i;
        for (i = 0; arg[i] != NULL; i++) {
            arg[i] = NULL;
        }
        input = NULL;
        output = NULL;

    }

    return 0;
}