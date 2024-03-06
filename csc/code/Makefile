CFLAGS = -I../include

all: fibo.o main.o

fibo.o: fibo.c ../include/fibo.h
	gcc $(CFLAGS) -c fibo.c

main.o: main.c ../include/fibo.h
	gcc $(CFLAGS) -c main.c

clean:
	rm ./*.o
