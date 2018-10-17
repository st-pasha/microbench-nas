
# CC = ${LLVM}/bin/clang++
# INCLUDES ?= -I.
# CCFLAGS += -std=gnu++11 -stdlib=libc++ -O3 -fopenmp -I${LLVM}/inclide -I${LLVM}/include/c++/v1
# LDFLAGS += -fopenmp -L${LLVM}/lib -Wl,-rpath,${LLVM}/lib

CC ?= clang++
CCFLAGS += -std=gnu++11 -stdlib=libc++ -O3

nas.o: nas.cc
	$(CC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

build: nas.o
	$(CC) $(LDFLAGS) -o na-benchmark $+ $(LIBRARIES)

clean:
	rm -f *.o na-benchmark
