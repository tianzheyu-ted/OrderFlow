
CXX      := g++
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -pedantic -Isrc


CORE_SRC    := src/OrderFlow.cpp
HEADERS     := src/OrderFlow.hpp src/IdGenerator.hpp src/RingBuffer.hpp
CORRECT_SRC := src/test/CorrectnessTests.cpp
STRESS_SRC  := src/test/StressTests.cpp


CORRECT_BIN := correctness
STRESS_BIN  := stress

.PHONY: all clean run_correctness run_stress


all: $(CORRECT_BIN) $(STRESS_BIN)


$(CORRECT_BIN): $(CORRECT_SRC) $(CORE_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CORRECT_SRC) $(CORE_SRC) -o $@


$(STRESS_BIN): $(STRESS_SRC) $(CORE_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(STRESS_SRC) $(CORE_SRC) -o $@


run_correctness: $(CORRECT_BIN)
	./$(CORRECT_BIN)

run_stress: $(STRESS_BIN)
	./$(STRESS_BIN)


clean:
	rm -f $(CORRECT_BIN) $(STRESS_BIN)