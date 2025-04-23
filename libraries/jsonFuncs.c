#include <stdbool.h>
#include "cJSON.h"

typedef struct Command {
    // Timestamp
    double key;
    char *binary_name;
    char *description;
} Command;

bool save_json_to_file(char* file_name, Command* cmd) {
    return true;
}
