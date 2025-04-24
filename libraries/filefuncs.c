#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "filefuncs.h"

// Structure of Json File
// {
//     "2034820389": {
//        "binary_name": "rm",
//        "command": "rm -rf /*",
//        "description": "Best command ever! Must try",
//     },
//     "2034820380": {
//        "binary_name": "rm",
//        "command": "rm -rf /*",
//        "description": "Best command ever! Must try",
//     },
// }
//
//

int write_to_file(char* file_name, char* json_str) {
    FILE* file_ptr = fopen(file_name, "a+");

    if (file_ptr == NULL) {
        perror("Error while opening file");
        return -1;
    }
    
    if (fwrite(json_str, 1, strlen(json_str), file_ptr) == 0) {
        perror("Error while writing to file");
        fclose(file_ptr);
        return -1;
    }
    fclose(file_ptr);

    return 0;
}

char* add_to_json(char* original_json, Command command) {
    cJSON* new_json = cJSON_CreateObject();

    cJSON_AddStringToObject(new_json, "binary_name", command.binary_name);
    cJSON_AddStringToObject(new_json, "command", command.command);
    cJSON_AddStringToObject(new_json, "description", command.description);

    cJSON* original;
    if (original_json != NULL) {
        original = cJSON_Parse(original_json);
    } else {
        original = cJSON_CreateObject();
    }

    if (original == NULL) {
        perror("Error while parsing json");
        exit(1);
    }

    cJSON_AddItemToObject(original, "AddTimeStampHere", new_json);

    char* final_json = cJSON_Print(original);

    return final_json;
}

char* read_entire_file(char* filename) {  
    FILE* file_ptr = fopen(filename, "r");

    if (file_ptr == NULL) {
        perror("ERROR: Could not read file");
        fclose(file_ptr);
        exit(1);
    }

    unsigned long length;
    fseek(file_ptr, 0, SEEK_END);
    length = ftell(file_ptr);
    rewind(file_ptr);

    char* buffer = malloc(length);
    fread(buffer, 1, length, file_ptr);

    fclose(file_ptr);

    return buffer;
}
