CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -Wno-missing-field-initializers -Wno-unused-parameter
corpcontrol: src/main.cpp src/*.hpp
	$(CXX) $(CXXFLAGS) src/main.cpp -o corpcontrol
clean:
	rm -f corpcontrol
.PHONY: clean
