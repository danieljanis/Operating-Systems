CC=g++
CFLAGS=-g -Wall -std=c++11
all: oss user_proc

oss: oss.o
		$(CC) oss.o -o oss

oss.o: oss.cpp
		$(CC) -c -g oss.cpp

user_proc: user.o
		$(CC) user.o -o user_proc

user.o: user.cpp
		$(CC) -c -g user.cpp

.PHONY: clean clean-all

clean:
		rm -rf *.o oss user_proc

clean-all:
		rm -rf *.o *.log oss user_proc
