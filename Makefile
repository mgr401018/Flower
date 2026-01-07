# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -O2 -Isrc -Iimports

# Detect operating system
# Detect operating system
ifeq ($(OS),Windows_NT)
    UNAME_S := Windows
else
    UNAME_S := $(shell uname -s)
endif


# Directories
SRC_DIR = src
BUILD_DIR = build
IMPORTS_DIR = imports

# Target executable name
TARGET = triangle

# Source files
SRCS = main.c \
       $(IMPORTS_DIR)/tinyfiledialogs.c \
       $(SRC_DIR)/text_renderer.c \
       $(SRC_DIR)/block_process.c \
       $(SRC_DIR)/block_input.c \
       $(SRC_DIR)/block_output.c \
       $(SRC_DIR)/block_assignment.c \
       $(SRC_DIR)/block_declare.c \
       $(SRC_DIR)/block_if.c \
       $(SRC_DIR)/block_converge.c \
       $(SRC_DIR)/block_cycle.c \
       $(SRC_DIR)/block_cycle_end.c \
       $(SRC_DIR)/code_exporter.c

# Object files (in build directory)
OBJS = $(SRCS:%.c=$(BUILD_DIR)/%.o)

# Libraries and flags based on OS
ifeq ($(UNAME_S),Linux)
    LIBS = -lglfw -lGL -lm
endif

ifeq ($(UNAME_S),Darwin)
    LIBS = -lglfw -framework OpenGL -framework Cocoa -framework IOKit
endif

ifeq ($(UNAME_S),Windows)
    TARGET = triangle.exe
    #LIBS = -lglfw3 -lopengl32 -lgdi32
    LIBS = -lglfw3 -lopengl32 -lgdi32 -lole32 -lcomdlg32 -loleaut32
endif


# Default target
all: $(BUILD_DIR)/$(TARGET)

# Create build directory structure
$(BUILD_DIR)/%.o: | $(BUILD_DIR) $(BUILD_DIR)/$(SRC_DIR) $(BUILD_DIR)/$(IMPORTS_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/$(SRC_DIR):
	mkdir -p $(BUILD_DIR)/$(SRC_DIR)

$(BUILD_DIR)/$(IMPORTS_DIR):
	mkdir -p $(BUILD_DIR)/$(IMPORTS_DIR)

# Link object files to create executable
$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$(TARGET) $(OBJS) $(LIBS)

# Generate embedded font header from TTF file
$(SRC_DIR)/embedded_font.h: $(IMPORTS_DIR)/DejaVuSansMono.ttf
	xxd -i $< > $@

# Compile source files to object files in build directory
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# text_renderer.c depends on embedded_font.h
$(BUILD_DIR)/$(SRC_DIR)/text_renderer.o: $(SRC_DIR)/text_renderer.c $(SRC_DIR)/embedded_font.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Run the program
run: $(BUILD_DIR)/$(TARGET)
	./$(BUILD_DIR)/$(TARGET)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET) triangle.exe

# Phony targets
.PHONY: all run clean
