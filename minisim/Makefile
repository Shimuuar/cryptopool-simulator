.SUFFIXES:
.PHONY: all clean test test-run

CXX           = g++
CXXFLAGS      = -Wall -g -O2 -std=c++17 -I..
LDFLAGS       =
CXXFLAGS_TEST = $(CXXFLAGS) -DBOOST_TEST_DYN_LINK
LDFLAGS_TEST  = $(LDFLAGS) -lboost_unit_test_framework
HEADERS  = $(wildcard *.hpp)
OBJ      = \
	main-stableswap-dynrate-lpf.cpp.o	\
	simulation.cpp.o			\
	sim-threading.cpp.o			\
	sim-data.cpp.o				\
	sim-json.cpp.o
TEST_OBJ = \
	test/test.cpp.o

all: main
clean:
	rm -fv *.o main test/*.o test/test_dummy

## ----------------------------------------------------------------
## Main executable

main: ${OBJ}
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $+ -o $@

%.cpp.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

foo: sim-json.cpp.o foo.cpp.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $+ -o $@

## ----------------------------------------------------------------
## Tests

test: test/test_dummy
	./test/test_dummy # --log_level=test_suite
test-run: main
	bash test-data/run.sh

test/test_dummy: $(TEST_OBJ)
	$(CXX) $(CXXFLAGS_TEST) $< -o $@ $(LDFLAGS_TEST)
test/%.cpp.o: test/%.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS_TEST) -c $< -o $@

