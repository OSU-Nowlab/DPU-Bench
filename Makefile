CC=mpicc
CFLAGS=-g -I./include -I/usr/include/infiniband -Wall
LDFLAGS= -lmlx5 -libverbs


EXES :=	cyclic-bcast.exe block-bcast.exe \
	cyclic-gather.exe block-gather.exe \
	cyclic-allgather.exe  \
	block-allgather.exe \
	multi-root-cyclic-allgather.exe \
	multi-lat.exe 


SRC := ./src
BIN := ./bin
OBJS := common.o rc.o compute_utils.o
HDRS := ./include/common.h ./include/compute_utils.h

default: $(EXES)
	rm -rf core*
	mkdir -p $(BIN)
	mv *.exe $(BIN)

%.o: $(SRC)/%.c $(HDRS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LDFLAGS)

%.exe: %.o $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

clean:
	rm -f *.o 
	rm -rf $(BIN)
	rm -rf core.*
	rm -f *.exe
