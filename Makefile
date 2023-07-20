CC := gcc
CC_FLAGS := -Wall
OUT := blob

all: blob.c
	$(CC) -o $(OUT) $^ $(CC_FLAGS)
