# Jakub Janeczko, 337670

CXX := g++
CXXFLAGS := -std=gnu++20 -Wall -Wextra -Werror -pedantic -O2
LINKERFLAG := -lm

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=%.o)

all: traceroute ${OBJECTS}

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

traceroute: $(OBJECTS) 
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@ $(LINKERFLAG)

clean:
	rm -f *.o

distclean: clean
	rm -f traceroute
