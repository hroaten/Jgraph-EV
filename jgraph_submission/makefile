CXX = g++-15 -Wall
BIN = Blackjack Check Plot_EV

CXXFLAGS = -std=c++17 -O2

OPENMP ?= 1
ifeq ($(OPENMP),1)
  CXXFLAGS += -fopenmp
  LINK_FLAGS = -fopenmp
endif

all: $(BIN)

Blackjack: play_bj.cpp bj.h bj.cpp
	$(CXX) $(CXXFLAGS) -o $@ play_bj.cpp bj.cpp $(LINK_FLAGS)

Plot_EV: plot_ev.cpp
	$(CXX) $(CXXFLAGS) -o $@ plot_ev.cpp

clean:
	rm -f $(BIN)
	rm -rf EV/