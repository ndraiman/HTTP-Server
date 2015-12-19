CC = gcc
CFLAGS = -c
OBJECTS = threadpool.o server.o
LDFLAGS = -lpthread

DEBUG_FLAGS = -g
DEBUG_OBJECTS = server.c

app: $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LDFLAGS) -o server

debug: $(DEBUG_OBJECTS)
	$(CC) $(DEBUG_FLAGS) $(DEBUG_OBJECTS) -Wall $(LDFLAGS) -o server

clean:
	rm $(OBJECTS)
	rm server


server.o: server.c
	$(CC) $(CFLAGS) $(LDFLAGS) server.c

threadpool.o: threadpool.c threadpool.h
	$(CC) $(CFLAGS) $(LDFLAGS) threadpool.c
