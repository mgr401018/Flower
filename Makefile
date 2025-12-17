# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -O2

# Detect operating system
UNAME_S := $(shell uname -s)

# Target executable name
TARGET = triangle

# Source files
SRCS = main.c imports/tinyfiledialogs.c imports/text_renderer.c

# Object files
OBJS = $(SRCS:.c=.o)

# Libraries and flags based on OS
ifeq ($(UNAME_S),Linux)
    LIBS = -lglfw -lGL -lm
endif
ifeq ($(UNAME_S),Darwin)
    LIBS = -lglfw -framework OpenGL -framework Cocoa -framework IOKit
endif
ifeq ($(OS),Windows_NT)
    TARGET = triangle.exe
    LIBS = -lglfw3 -lopengl32 -lgdi32
endif

# Default target
all: $(TARGET)

# Link object files to create executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

# Compile source files to object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Run the program
run: $(TARGET)
	./$(TARGET)

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET) triangle.exe

# Phony targets
.PHONY: all run clean