CC = g++
CFLAGS = -Wall -Wextra
LDFLAGS = -pthread
INCLUDE = ./include
SOURCE = boost.cpp

boost: $(SOURCE)
	$(CC) $(SOURCE) -I $(INCLUDE) $(LDFLAGS) -o $@

run: boost
	./boost

clean:
	rm boost
