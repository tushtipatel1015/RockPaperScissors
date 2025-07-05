CC = gcc
CFLAGS = -Wall -Werror -Wextra -g
TARGET = rpsd
OBJS = rpsd.o network.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

rpsd.o: rpsd.c network.h
	$(CC) $(CFLAGS) -c rpsd.c

network.o: network.c network.h
	$(CC) $(CFLAGS) -c network.c

clean:
	rm -f $(TARGET) *.o
