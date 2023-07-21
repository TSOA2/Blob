CC := gcc
CC_FLAGS := -Wall -g
OUT := blob

all: blob.o
	$(CC) -o $(OUT) $^ $(CC_FLAGS)

blob.o: blob.c
	$(CC) -c -o $@ $^ $(CC_FLAGS)

clean:
	rm -fR *.o
