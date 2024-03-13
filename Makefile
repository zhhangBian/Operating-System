all:
	gcc -o hello hello.c

run: hello
	./hello

clean:
	rm hello
