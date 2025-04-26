compile: save-cmd.c libraries/filefuncs.c
	gcc -o save-cmd -Wall -Werror -Wextra -ggdb save-cmd.c libraries/filefuncs.c libraries/cJSON.o

run: save-cmd
	./save-cmd

clean: save-cmd
	rm save-cmd

test: test.c libraries/filefuncs.c
	gcc -o test -Wall -Werror -Wextra -ggdb test.c libraries/cJSON.o libraries/filefuncs.c

run_test: test.c libraries/filefuncs.c
	gcc -o test -Wall -Werror -Wextra -ggdb test.c libraries/cJSON.o libraries/filefuncs.c && ./test
