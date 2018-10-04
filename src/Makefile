CC = g++
LD = g++
MKDIR_P = mkdir -p

CFLAGS = -g -O3 -Wall -c -I../include
LDFLAGS = -g -Wall -O0
LIBRARIES += -L../lib -l:libzstd.a

SOURCES=main.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=hprof-rewriter
OBJ_DIR=../obj
BIN_DIR=../bin

.PHONY: directories

all: directories $(EXECUTABLE) $(SOURCES)

directories: $(OBJ_DIR) $(BIN_DIR)

$(OBJ_DIR):
	$(MKDIR_P) $(OBJ_DIR)

$(BIN_DIR):
	$(MKDIR_P) $(BIN_DIR)

$(EXECUTABLE): $(SOURCES) $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJ_DIR)/$(OBJECTS) $(LIBRARIES) -o $(BIN_DIR)/$@

$(OBJECTS): $(SOURCES)
	$(CC) $(CFLAGS) -c main.cpp -o $(OBJ_DIR)/$@

clean:
	rm -rf $(BIN_DIR) $(OBJ_DIR)
