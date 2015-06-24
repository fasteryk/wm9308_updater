CC = gcc

TARGET = updater

INC_PATH := $(shell pkg-config --cflags gtk+-3.0)
INC_PATH += -I/usr/include/libxml2  

LIB_PATH := $(shell pkg-config --libs gtk+-3.0)
LIB_PATH += -lxml2  

			
CFLAGS += -g $(INC_PATH) 
LDFLAGS += -export-dynamic $(LIB_PATH)

OBJS = main.o

all:$(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
	rm -f $(TARGET)

