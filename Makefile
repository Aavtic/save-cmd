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

install: save-cmd.c
	@echo "Compiling save-cmd..."
	mkdir -p bin
	gcc -o bin/save-cmd -Wall -Werror -Wextra -ggdb save-cmd.c libraries/filefuncs.c libraries/cJSON.o

	@echo "Creating startup script..."
	printf '#!/bin/bash\n\ncd %s\n./bin/save-cmd "$$@"\n' "$(realpath .)" > ./save-cmd.sh
	chmod +x ./save-cmd.sh

	@echo "Saving a link to ~/.local/bin/"
	mkdir -p ~/.local/bin
	ln -sf $(realpath ./save-cmd.sh) ~/.local/bin/save-cmd

	@echo "Installation Complete!"
