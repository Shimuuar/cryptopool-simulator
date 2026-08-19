.SUFFIXES:
.PHONY: all clean test test-run

CXX           = g++
CXXFLAGS      = -Wall -g -O2 -std=c++20 -I..
LDFLAGS       =
CXXFLAGS_TEST = $(CXXFLAGS) -I. $(shell pkg-config --cflags gtest_main)
LDFLAGS_TEST  = $(LDFLAGS) $(shell pkg-config --libs gtest_main)
LIB_SRC = \
	simulation.cpp			\
	sim-threading.cpp		\
	sim-data.cpp			\
	sim-json.cpp
TEST_SRC = \
	test/test.cpp

LIB_OBJ  = $(patsubst %,      build/%.o,      $(LIB_SRC))
TEST_OBJ = $(patsubst test/%, build/test/%.o, $(TEST_SRC))

# Compiler-generated dependency files (one per object).
LIB_DEP  = $(LIB_OBJ:.cpp.o=.cpp.d)
TEST_DEP = $(TEST_OBJ:.cpp.o=.cpp.d)

all: main
clean:
	rm -fv build/* test/build/* *.o *.d *.a main test/*.o test/*.d test/test_dummy

## ----------------------------------------------------------------
## Main executable

main: libminisim.a build/main-stableswap-dynrate-lpf.cpp.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) build/main-stableswap-dynrate-lpf.cpp.o libminisim.a -o $@
libminisim.a: ${LIB_OBJ}
	ar crs $@ $+
# -MMD writes precise header deps to *.cpp.d
build/%.cpp.o: %.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

foo: sim-json.cpp.o foo.cpp.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $+ -o $@

## ----------------------------------------------------------------
## Tests

test: test/test
	./test/test
test-run: main
	bash test-data/run.sh

test/test: $(TEST_OBJ) libminisim.a
	$(CXX) $(CXXFLAGS_TEST) $(TEST_OBJ) libminisim.a -o $@ $(LDFLAGS_TEST)
build/test/%.cpp.o: test/%.cpp
	mkdir -p build/test
	$(CXX) $(CXXFLAGS_TEST) -MMD -c $< -o $@

## ----------------------------------------------------------------
## Auto-generated header dependencies
-include $(LIB_DEP) $(TEST_DEP) build/main-stableswap-dynrate-lpf.cpp.d
