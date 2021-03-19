CC=g++
CFLAGS=-g -Wall -std=c++11
all: oss user

oss: oss.o
		$(CC) oss.o -o oss

oss.o: oss.cpp
		$(CC) -c -g oss.cpp

user: user.o
		$(CC) user.o -o user

user.o: user.cpp
		$(CC) -c -g user.cpp

.PHONY: clean clean-all

clean:
		rm -rf *.o oss user

clean-all:
		rm -rf *.o *.log oss user
