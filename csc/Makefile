out: fibo.o main.o
	gcc fibo.o main.o -o fibo

fibo.o: ./include/fibo.h
	gcc -Ipath ./include/fibo.h
	gcc -c ./code/fibo.c

main.o: ./include/fibo.h
	gcc -c ./code/main.c

clean:
	rm ./*.o
