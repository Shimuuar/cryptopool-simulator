.SUFFIXES:
.PHONY: all clean test

CXX      = g++
CXXFLAGS = -Wall -g -O2 -std=c++17 -I..
LDFLAGS  =

all: main
clean:
	rm -fv *.o main

test: main
	bash test/run.sh

main: main-stableswap-dynrate-lpf.cpp.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $+ -o $@

%.cpp.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
