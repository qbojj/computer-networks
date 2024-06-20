# Jakub Janeczko, 337670

CXX := g++
CXXFLAGS := -std=gnu++20 -Wall -Wextra -Werror -pedantic -g -Og #-O2
LINKERFLAG := -lm

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=%.o)

all: transport ${OBJECTS}

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

transport: $(OBJECTS) 
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@ $(LINKERFLAG)

clean:
	rm -f *.o

distclean: clean
	rm -f transport
