#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "filefuncs.h"


static const char* ARRAY[JSON_LENGTH] = {"binary_name", "command", "description"};

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
    FILE* file_ptr = fopen(file_name, "w");

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

    char timestamp[10];
    sprintf(timestamp, "%ld", command.timestamp);

    cJSON_AddItemToObject(original, timestamp, new_json);

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

bool  check_string_exists(char* string, char* pattern) {
    size_t size_str = strlen(string);
    size_t size_pattern = strlen(pattern);

    if (size_pattern > size_str)
        return false;

    for (size_t i=0; i<=size_str - size_pattern; ++i) {
        if (strncmp(&string[i], pattern, size_pattern) == 0)
            return true;
    }

    return false;
}


void print_command_info(char* binary_name, char* command, char* description) {
    printf("Binary Name: %s\n", binary_name);
    printf("Command    : %s\n", command);
    printf("Description: %s\n", description);
}

bool search_json(char* json_str, char* pattern) {
    cJSON* json = cJSON_Parse(json_str);

    if (json == NULL) {
        printf("ERROR: Can't parse json");
        exit(1);
    }
    json = json->child;

    bool found = false;

    while (json != NULL) {
        cJSON* binary_name_json = cJSON_GetObjectItemCaseSensitive(json, "binary_name");
        cJSON* command_json = cJSON_GetObjectItemCaseSensitive(json, "command");
        cJSON* description_json = cJSON_GetObjectItemCaseSensitive(json, "description");

        char* binary_name;
        char* command;
        char* description;

        if ((binary_name_json != NULL && binary_name_json->valuestring != NULL) && 
                (command_json != NULL && command_json->valuestring != NULL)) {

            binary_name = binary_name_json->valuestring;
            command = command_json->valuestring;
        } else {
            printf("%s", json->string);
            printf("Faulty JSON\n");
            return false;
        }

        if (description_json != NULL && description_json->valuestring != NULL) {
            if (strcmp(description_json->valuestring, "") == 0)
                description = "No Description";
            else
                description = description_json->valuestring;
        }

        for (int i=0; i<JSON_LENGTH; ++i) {
            const char* field = ARRAY[i];

            cJSON* json_field = cJSON_GetObjectItemCaseSensitive(json, field);
            if (json_field != NULL && json_field->valuestring != NULL) {
                char copy_str[strlen(json_field->valuestring) + 1];
                strcpy(copy_str, json_field->valuestring);
                copy_str[strlen(json_field->valuestring)] = '\0';
                if (check_string_exists(copy_str, pattern)) {
                    print_command_info(binary_name, command, description);
                    printf("\n");
                    found = true;
                    break;
                }
            }
        }
        json = json->next;
    }
    return found;
}

void print_json(char* json_str) {
    cJSON* json = cJSON_Parse(json_str);

    if (json == NULL) {
        printf("ERROR: Can't parse json");
        exit(1);
    }
    json = json->child;

    while (json != NULL) {
        cJSON* binary_name_json = cJSON_GetObjectItemCaseSensitive(json, "binary_name");
        cJSON* command_json = cJSON_GetObjectItemCaseSensitive(json, "command");
        cJSON* description_json = cJSON_GetObjectItemCaseSensitive(json, "description");

        char* binary_name;
        char* command;
        char* description;

        if ((binary_name_json != NULL && binary_name_json->valuestring != NULL) && 
                (command_json != NULL && command_json->valuestring != NULL)) {

            binary_name = binary_name_json->valuestring;
            command = command_json->valuestring;
        } else {
            printf("%s", json->string);
            printf("Faulty JSON\n");
            return;
        }

        if (description_json != NULL && description_json->valuestring != NULL) {
            if (strcmp(description_json->valuestring, "") == 0)
                description = "No Description";
            else
                description = description_json->valuestring;
        }

        print_command_info(binary_name, command, description);
        printf("\n");

        json = json->next;
    }
}
