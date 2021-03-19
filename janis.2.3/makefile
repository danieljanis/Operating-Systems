CC=g++
CFLAGS=-g -Wall -std=c++11
all: master palin

master: master.o
		$(CC) master.o -o master

master.o: master.cpp
		$(CC) -c master.cpp

palin: palin.o
		$(CC) palin.o -o palin

palin.o: palin.cpp
		$(CC) -c palin.cpp

.PHONY: clean
clean:
		rm -rf *.o *.out *.log master palin 
