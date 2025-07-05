CC = gcc
CFLAGS = -g -Wall -std=c99 -fsanitize=address,undefined -pthread

rc: rc.o network.o
	$(CC) $(CFLAGS) -o $@ $^

network.o rc.o: network.h


clean:
	rm -rf *.o rc
