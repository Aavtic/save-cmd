compile: main.c
	gcc -o main -Wall -Werror -Wextra -ggdb main.c

run: main
	./main

clean: main
	rm main

test: test.c libraries/filefuncs.c
	gcc -o test -Wall -Werror -Wextra -ggdb test.c libraries/cJSON.o libraries/filefuncs.c

run_test: test.c libraries/filefuncs.c
	gcc -o test -Wall -Werror -Wextra -ggdb test.c libraries/cJSON.o libraries/filefuncs.c && ./test
