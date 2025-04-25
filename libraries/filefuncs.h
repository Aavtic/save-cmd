#ifndef FILE_FUNCS
#define FILE_FUNCS


typedef struct Command {
    unsigned long timestamp;
    char* binary_name;
    char* command;
    char* description;
} Command;


int write_to_file(char* file_name, char* json_str);
char* add_to_json(char* original_json, Command command); 
char* read_entire_file(char* filename);
void search_json(char* json_str);

#endif // ifndef FILE_FUNCS
