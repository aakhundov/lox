CC=gcc
CFLAGS=-c -std=c11 -Wall -Werror -fPIC -g
LDFLAGS=-g

APP=clox
APP_LIB=$(APP)-lib
SRC_DIR=clox
BIN_DIR=bin

RUNNER=$(BIN_DIR)/$(APP)
LIB=$(BIN_DIR)/$(APP_LIB).so

SOURCES=$(wildcard $(SRC_DIR)/*.c)
RUNNER_SOURCES=$(SRC_DIR)/main.c
RUNNER_OBJECTS=$(RUNNER_SOURCES:$(SRC_DIR)/%.c=$(BIN_DIR)/%.o)
LIB_SOURCES=$(filter-out $(RUNNER_SOURCES), $(SOURCES))
LIB_OBJECTS=$(LIB_SOURCES:$(SRC_DIR)/%.c=$(BIN_DIR)/%.o)

all: clox clox-lib

$(APP): $(RUNNER)
$(APP_LIB): $(LIB)

$(RUNNER): $(RUNNER_OBJECTS) $(LIB)
	$(CC) $(LDFLAGS) $^ -o $@

$(RUNNER_OBJECTS): $(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $< -o $@

$(LIB): $(LIB_OBJECTS)
	$(CC) -shared $(LDFLAGS) $^ -o $@

$(LIB_OBJECTS): $(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf $(BIN_DIR)
	rm -rf jlox/*.class

.PHONY: clean

$(shell mkdir -p $(BIN_DIR))
