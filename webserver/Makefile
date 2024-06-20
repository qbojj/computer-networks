# Jakub Janeczko, 337670

CXX := g++
CXXFLAGS := -std=gnu++20 -Wall -Wextra -Werror -pedantic -g #-O2
LINKERFLAG := -lm

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=%.o)

all: webserver ${OBJECTS}

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

webserver: $(OBJECTS) 
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@ $(LINKERFLAG)

clean:
	rm -f *.o

distclean: clean
	rm -f webserver
