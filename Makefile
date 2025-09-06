CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2
LIBS = -lncurses
OBJS = monitor.o cpu.o memory.o tui.o

all: monitor

monitor: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(OBJS) monitor

