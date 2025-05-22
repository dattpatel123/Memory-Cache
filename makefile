# Makefile

CC = gcc
LDFLAGS = -lpthread -lrt
TARGET = mcached
SRC = mcached.c

# Default rule
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) -Wall -Wextra -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
