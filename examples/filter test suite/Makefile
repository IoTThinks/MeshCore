CXX      = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -I.

SRCS_COMMON = FilterParser.cpp ChannelFilter.cpp

all: auto_test shell

auto_test: $(SRCS_COMMON) auto_test.cpp
	$(CXX) $(CXXFLAGS) -o auto_test $(SRCS_COMMON) auto_test.cpp

shell: $(SRCS_COMMON) shell.cpp
	$(CXX) $(CXXFLAGS) -o shell $(SRCS_COMMON) shell.cpp

run_tests: auto_test
	./auto_test

clean:
	rm -f auto_test shell
