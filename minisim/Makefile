.SUFFIXES:
.PHONY: all clean test

CXX      = g++
CXXFLAGS = -Wall -g -O2 -std=c++17 -I..
LDFLAGS  =
HEADERS  = $(wildcard *.hpp)
OBJ      = \
	main-stableswap-dynrate-lpf.cpp.o	\
	simulation.cpp.o			\
	sim-threading.cpp.o			\
	sim-data.cpp.o


all: main
clean:
	rm -fv *.o main

test: main
	bash test/run.sh

main: ${OBJ}
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $+ -o $@

%.cpp.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

foo: sim-json.cpp.o foo.cpp.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $+ -o $@
