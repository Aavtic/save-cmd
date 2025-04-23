#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/*#include "cJSON.h"*/

typedef struct {
    bool insert;
    bool execute;
    bool usage;
    // TODO: Change these str pointer to arrays to it actually owns them00
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



void print_usage(FILE *file) {
    const char* usage = "Usage: save-cmd  [-i] [-x]\n\
        [-d <description>] [/<search>]\n\
        <file> ...";
    fprintf(file, "%s\n", usage);
}

void validate_arguments(CommandLineArguments cla) {
    // If both command and search is specified.
    // Here it is not possible to identify what flag the user was actually trying to execute
    if (cla.command != NULL && cla.search != NULL) {
        fprintf(stderr, "%s, %s\t\t%s\n", cla.command, cla.search, "ERROR: Use of both /<search> and \"<command>\" Flags.\nBoth of these commands cannot be used togather");
        print_usage(stderr);
        exit(1);
    }
    if (cla.command != NULL) {
        if (cla.search != NULL || cla.usage || cla.execute || cla.insert) {
            char incorrect_flags_used[256] = "";

            if (cla.search != NULL) {
                char* message = "\t\tSearch flag used\n";
                char* buffer = malloc(strlen(cla.search) + 1 + strlen(message));
                sprintf(buffer,"%s%s", cla.search, message);
                strcat(incorrect_flags_used, buffer);
            }

            if (cla.insert) {
                strcat(incorrect_flags_used, "-i\t\tInsert flag used\n");
            }

            if (cla.execute) {
                strcat(incorrect_flags_used, "-x\t\tExecute flag used\n");
            }

            if (cla.usage) {
                strcat(incorrect_flags_used, "-h | --help\t\tFlag used");
            }

            fprintf(stderr, "%s\n", "ERROR: Use of one or more flags which cannot be used togather when adding a command\n");
            fprintf(stderr, "%s\n", incorrect_flags_used);
            print_usage(stderr);
            exit(1);
        }
    } else if (cla.search != NULL) {
        if (cla.command != NULL || cla.usage) {
            char incorrect_flags_used[256] = "";

            if (cla.command != NULL) {
                char* message = "\t\tCommand flag used\n";
                char* buffer = malloc(strlen(cla.command) + 1 + strlen(message));
                sprintf(buffer,"%s%s", cla.command, message);
                strcat(incorrect_flags_used, buffer);
            }

            if (cla.insert) {
                strcat(incorrect_flags_used, "-i\t\tInsert flag used\n");
            }

            if (cla.execute) {
                strcat(incorrect_flags_used, "-x\t\tExecute flag used\n");
            }

            if (cla.usage) {
                strcat(incorrect_flags_used, "-h | --help\t\tFlag used");
            }

            fprintf(stderr, "%s\n", "ERROR: Use of one or more flags which cannot be used togather when adding a command\n");
            fprintf(stderr, "%s\n", incorrect_flags_used);
            print_usage(stderr);
            exit(1);
        }
    }
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
                exit(1);
            }
        } else if (argument[0] == '/') {
            argument++;
            cla.search = argument;
        } else if (strcmp(argument, "-h") == 0 || strcmp(argument, "--help") == 0) {
            print_usage(stdout);
            exit(0);
        } else if (argument[0] == '/' || argument[0] == '-') {
            fprintf(stderr, "%s: %s\n", "ERROR: Usage of unknown flag", argument);
            print_usage(stderr);
        } else {
            // case for command to save.
            if (cla.command == NULL) {
                cla.command = argument;
            } else {
                fprintf(stderr, "%s: %s\n", "ERROR: Usage of unknown or extra arguments", argument);
                print_usage(stderr);
                exit(1);
            }
        }
    }

    validate_arguments(cla);
}

int main(int argc, char* argv[]) {
    parse_arguments(argc, argv);

    return 0;
}
