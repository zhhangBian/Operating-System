CFLAGS = -I./include

all: fibo

fibo.o: ./code/fibo.c ./include/fibo.h
	gcc $(CFLAGS) -c ./code/fibo.c

main.o: ./code/main.c ./include/fibo.h
	gcc $(CFLAGS) -c ./code/main.c

fibo: fibo.o main.o
	gcc -o fibo fibo.o main.o

clean:
	rm ./*.o
