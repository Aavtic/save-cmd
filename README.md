> [!WARNING]
Under Construction...

# Save Commands

Tool to save commands and retrieve them quickly.


## Usage

Save a command
```sh
# save-cmd "<command>" -d "description"
save-cmd "git restore --source <commit hash> filename" -d "Restore a file to it's older commit form in git"
```

Search for a saved command
```
#save-cmd /<any search term in the command saved or description>
save-cmd /restore

Binary Name: git
Command    : git restore --source <commit hash> filename
Description: Restore a file to it's older commit form in git
```

Show all the saved commands
Just run the program without any arguments to show all the saved commands.
```
save-cmd
```


## Installation

First clone this repository
```sh
```


## Installation

First clone this repository
```sh
```


## Installation

First clone this repository
```sh
git clone https://github.com/aavtic/save-cmd.git
```

1. Now you can install using `make` utility.
run make intall
```sh
make install
```

Otherwise if you wish to just compile the program
run make compile
```sh
make compile
```

This saves the compiled program to the current directory
