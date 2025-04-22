#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
/*#include "cJSON.h"*/

typedef struct {
    bool insert;
    bool execute;
    bool usage;
    char* search;
    char* command;
    char* description;
} CommandLineArguments;

/*
   Expected Arguments
   "<command>"          - Command to save
   "/<keyword>"         - Key Word to search through the saved files
   "-i"                 - Insert first result from the search to screen (Should be used with one of the search flags)
   "-x"                 - Execute first result from the search to screen (Should be used with one of the search flags)
   "-d" "<Description>" - Add a description to the command
   "-h"                 - Prints usage
 */

// save-cmd "rm -rf /*"
// save-cmd "rm -rf /*" -d "Best command ever!"
// save-cmd /hello
// save-cmd /hello -x
// save-cmd /hello -i


/*void validate_arguments(CommandLineArguments cla) {*/
/*}*/

void print_usage(FILE *file) {
    const char* usage = "Usage: save-cmd  [-i] [-x]\n\
        [-d <description>] [/<search>]\n\
        <file> ...";
    fprintf(file, "%s\n", usage);
}

void parse_arguments(int argc, char* argv[]) {
    // Skip the binary name
    argv++;
    argc--;

    CommandLineArguments cla = {0};

    for (int i=0; i<argc; ++i) {
        char* argument = argv[i];

        if (strcmp(argument, "-i") == 0) {
            cla.insert = true;
        } else if (strcmp(argument, "-x") == 0) {
            cla.execute = true;
        } else if (strcmp(argument, "-d") == 0) {
            if (i + 1 < argc) {
                char* value = argv[i + 1];
                cla.description = value;
                i++;
            } else {
                fprintf(stderr, "%s\n", "ERROR: -d flag found but no description provided!" );
                print_usage(stderr);
                exit(0);
            }
        } else if (argument[0] == '/') {
            argument++;
            cla.search = argument;
        } else if (strcmp(argument, "-h") == 0 || strcmp(argument, "--help") == 0) {
            print_usage(stdout);
            exit(0);
        } else if (argument[0] == '/' || argument[0] == '-') {
            print_usage(stderr);
        } else {
            // case for command to save.
            if (cla.command == NULL) {
                cla.command = argument;
            } else {
                fprintf(stderr, "%s: %s\n", "ERROR: Usage of unknown or extra arguments", argument);
                print_usage(stderr);
                exit(0);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    parse_arguments(argc, argv);

    return 0;
}
