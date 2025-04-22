compile: main.c
	gcc -o main -Wall -Werror -Wextra -ggdb main.c

run: main
	./main

clean: main
	rm main
