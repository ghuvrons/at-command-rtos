DIR_ROOT := /home/janoko/Project/C/at-command
BUILD_DIR := $(DIR_ROOT)/build
SRC_DIRS := $(DIR_ROOT)/src
TEST_DIRS := $(DIR_ROOT)/example
CONFIG_PATH := $(DIR_ROOT)/example/config.h
INC_CONFIG := -include"$(CONFIG_PATH)"

COMPILER := /usr/bin/gcc
CFLAG := -fPIC

INC_DIR := $(SRC_DIRS)/include
SRCS := $(shell find $(SRC_DIRS) -name '*.cpp' -or -name '*.c' -or -name '*.s')
OBJS := $(patsubst $(SRC_DIRS)/%.c,$(BUILD_DIR)/obj/%.o,$(SRCS))

LIB_DIR := $(BUILD_DIR)/lib
BUILDLIB := $(LIB_DIR)/libat-cmd.so
BUILDLIBSFLAG := $(patsubst $(LIB_DIR)/lib%.so,-l%,$(BUILDLIB))

all: configure
	Done

debug: $(OBJS)
	$(COMPILER) $(OBJS) -g $(TEST_DIRS)/main.c -I$(INC_DIR) $(INC_CONFIG) -o $(BUILD_DIR)/main
	chmod 700 $(BUILD_DIR)/main

run: build
	$(COMPILER) -g $(TEST_DIRS)/main.c -I$(INC_DIR) $(INC_CONFIG) -L$(BUILD_DIR)/lib $(BUILDLIBSFLAG) -o $(BUILD_DIR)/main
	chmod 700 $(BUILD_DIR)/main
	export LD_LIBRARY_PATH=$(DIR_ROOT)/build/lib/
	$(DIR_ROOT)/build/main

build: $(OBJS)
	mkdir -p $(BUILD_DIR)/lib
	$(COMPILER) $(OBJS) -shared -o $(BUILD_DIR)/lib/libat-cmd.so $(INC_CONFIG)

# Build step for C source
$(BUILD_DIR)/lib/lib%.so: $(BUILD_DIR)/obj/%.o
	mkdir -p $(dir $@)
	$(COMPILER) $< -shared -o $@ $(INC_CONFIG)

# Build step for C source
$(BUILD_DIR)/obj/%.o: $(SRC_DIRS)/%.c
	mkdir -p $(dir $@)
	$(COMPILER) -c $< -I$(INC_DIR) -o $@ $(CFLAG) $(INC_CONFIG)


.PHONY: clean
clean:
	rm -r $(BUILD_DIR)