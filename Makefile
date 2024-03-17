CC = g++
CFLAGS = -Wall -Wextra
LDFLAGS = -pthread
INCLUDE = ./include
SOURCE = boost.cpp Entity.cpp
OBJS = $(SOURCE:.cpp=.o)
TARGET = boost
TARGET_DEBUG = boost_debug

$(TARGET): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

%.o: %.cpp
	$(CC) -I $(INCLUDE) -c $< -o $@

$(TARGET_DEBUG): $(OBJS)
	$(CC) $^ $(LDFLAGS) -g -o $@

run: $(TARGET)
	./$<

debug: $(TARGET_DEBUG)
	gdb ./$<

clean:
	rm -f $(TARGET) $(TARGET_DEBUG) $(OBJS)
