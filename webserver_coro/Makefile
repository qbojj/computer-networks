# Jakub Janeczko, 337670

CXX := g++
CXXFLAGS := -std=gnu++20 -g -MMD -Wall -Wextra -Wpedantic # -O2
LINKERFLAG := -lm

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:%.cpp=%.o)
HEADERS := $(wildcard *.h *.hpp)
DEPS := $(OBJECTS:%.o=%.d)

all: webserver ${OBJECTS}

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

webserver: $(OBJECTS) 
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@ $(LINKERFLAG)

clean:
	rm -f $(DEPS) $(OBJECTS)

distclean: clean
	rm -f webserver

-include $(OBJECTS:%.o=%.d)
