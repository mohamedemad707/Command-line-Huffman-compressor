CXX      ?= g++
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra -Isrc

LIB_SRC  = src/BitIoStream.cpp src/CanonicalCode.cpp src/CodeTree.cpp src/FrequencyTable.cpp \
           src/HuffmanCoder.cpp src/Compressor.cpp src/TreeExport.cpp
LIB_OBJ  = $(LIB_SRC:src/%.cpp=build/%.o)

.PHONY: all test sanitize clean
all: huffman

huffman: $(LIB_OBJ) build/main.o
	$(CXX) $(CXXFLAGS) -o $@ $^

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build:
	mkdir -p build

# Unit tests (run in a scratch directory so test files do not clutter the project)
test: $(LIB_OBJ) tests/test_huffman.cpp
	$(CXX) $(CXXFLAGS) -o build/run_tests tests/test_huffman.cpp $(LIB_OBJ)
	cd build && ./run_tests

# Same tests with AddressSanitizer + UndefinedBehaviorSanitizer
sanitize: tests/test_huffman.cpp $(LIB_SRC)
	mkdir -p build
	$(CXX) -std=c++17 -g -O1 -Wall -Wextra -Isrc -fsanitize=address,undefined -fno-sanitize-recover=undefined \
		-o build/run_tests_san tests/test_huffman.cpp $(LIB_SRC)
	cd build && ./run_tests_san

clean:
	rm -rf build huffman
