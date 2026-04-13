CC=gcc
CFLAGS=-Wall -Wextra -std=c99

VERSION=$(shell cat VERSION)
CFLAGS += -DAPP_VERSION=\"$(VERSION)\"

SRC=$(wildcard src/*.c)
OBJ=$(SRC:.c=.o)

TARGET=tg_bot

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ -lcurl -lcjson

clean:
	rm -f $(OBJ) $(TARGET)
