CC = g++
CFLAGS = -Wall -Wextra
LDFLAGS = -pthread
INCLUDE = ./include
SOURCE = boost.cpp

boost: $(SOURCE)
	$(CC) $(SOURCE) -I $(INCLUDE) $(LDFLAGS) -o $@

boost_debug: $(SOURCE)
	$(CC) $(SOURCE) -I $(INCLUDE) $(LDFLAGS) -g -o $@

run: boost
	./boost

debug: boost_debug
	gdb ./boost_debug

clean:
	rm boost
