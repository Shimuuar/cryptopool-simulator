.SUFFIXES:
.PHONY: all clean test test-run

CXX           = g++
CXXFLAGS      = -Wall -g -O2 -std=c++17 -I..
LDFLAGS       =
CXXFLAGS_TEST = $(CXXFLAGS) -DBOOST_TEST_DYN_LINK
LDFLAGS_TEST  = $(LDFLAGS) -lboost_unit_test_framework
OBJ      = \
	main-stableswap-dynrate-lpf.cpp.o	\
	simulation.cpp.o			\
	sim-threading.cpp.o			\
	sim-data.cpp.o				\
	sim-json.cpp.o
TEST_OBJ = \
	test/test.cpp.o

# Compiler-generated dependency files (one per object).
DEP      = $(OBJ:.cpp.o=.cpp.d)
TEST_DEP = $(TEST_OBJ:.cpp.o=.cpp.d)

all: main
clean:
	rm -fv *.o *.d main test/*.o test/*.d test/test_dummy

## ----------------------------------------------------------------
## Main executable

main: ${OBJ}
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $+ -o $@

# -MMD writes precise header deps to *.cpp.d
%.cpp.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

foo: sim-json.cpp.o foo.cpp.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $+ -o $@

## ----------------------------------------------------------------
## Tests

test: test/test
	./test/test # --log_level=test_suite
test-run: main
	bash test-data/run.sh

test/test: $(TEST_OBJ)
	$(CXX) $(CXXFLAGS_TEST) $< -o $@ $(LDFLAGS_TEST)
test/%.cpp.o: test/%.cpp
	$(CXX) $(CXXFLAGS_TEST) -MMD -c $< -o $@

## ----------------------------------------------------------------
## Auto-generated header dependencies
-include $(DEP) $(TEST_DEP)
