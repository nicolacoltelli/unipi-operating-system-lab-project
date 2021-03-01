BIN = ./bin/
INCLUDE = ./include/
LIB = ./lib/
SRC = ./src/
TEST = ./test/
LOGS = ./logs/

CC = gcc
CFLAGS = -g -pedantic -Wall -O3 -D_POSIX_C_SOURCE=200809L
INCLUDES = -I $(INCLUDE)
LFLAGS = -L $(LIB) -Wl,-rpath=$(LIB)
LIBS = -lfifo_unbounded -lpthread

OBJECTS = $(SRC)cashier.o $(SRC)supermarket.o $(SRC)utils.o \
			$(SRC)customer.o $(SRC)director.o

TARGETS = $(BIN)supermarket $(LIB)libfifo_unbounded.so

.PHONY: all test start startandquit \
			memory memoryquit \
			clean cleanall cleanlogs

all: $(TARGETS)

$(BIN)supermarket: $(OBJECTS) $(LIB)libfifo_unbounded.so
	mkdir $(BIN)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJECTS) -o $@ $(LFLAGS) $(LIBS)

$(LIB)libfifo_unbounded.so: $(SRC)fifo_unbounded.o
	mkdir $(LIB)
	$(CC) -shared $< -o $@

$(SRC)supermarket.o: $(SRC)supermarket.c
	$(CC) $(CFLAGS) -c $(INCLUDES) $< -o $@

$(SRC)director.o: $(SRC)director.c
	$(CC) $(CFLAGS) -c $(INCLUDES) $< -o $@

$(SRC)cashier.o: $(SRC)cashier.c
	$(CC) $(CFLAGS) -c $(INCLUDES) $< -o $@

$(SRC)customer.o: $(SRC)customer.c
	$(CC) $(CFLAGS) -c $(INCLUDES) $< -o $@

$(SRC)utils.o: $(SRC)utils.c
	$(CC) $(CFLAGS) -c $(INCLUDES) $< -o $@

$(SRC)fifo_unbounded.o: $(SRC)fifo_unbounded.c
	$(CC) $(CFLAGS) -c -fpic $(INCLUDES) $< -o $@

test:
	-rm $(LOGS)*.log;
	printf "test started\n"
	./bin/supermarket & \
	sleep 25;			\
	kill -s 1 $$!;		\
	wait $$!;			\
	./script/analisi.sh

start:
	./bin/supermarket & \
	sleep 25;			\
	kill -s 1 $$!;		\
	wait $$!;			\
	printf "test completed\n"

startandquit:
	./bin/supermarket & \
	sleep 25;			\
	kill -s 3 $$!;		\
	wait $$!;			\
	printf "test completed\n"

memory:
	valgrind --leak-check=full --show-leak-kinds=all ./bin/supermarket & \
	sleep 25;			\
	kill -s 1 $$!;		\
	wait $$!;			\
	printf "test completed\n" 

memoryquit:
	valgrind --leak-check=full --show-leak-kinds=all ./bin/supermarket & \
	sleep 25;			\
	kill -s 3 $$!;		\
	wait $$!;			\
	printf "test completed\n" 

clean:
	-rm $(BIN)supermarket
	-rm $(LIB)libfifo_unbounded.so

cleanall:
	make clean
	make cleanlogs
	-rm $(SRC)*.o

cleanlogs:
	-rm $(LOGS)*.log
