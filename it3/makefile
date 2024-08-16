CXX = g++
CXXFLAGS = -Wall -std=c++11 -g

TARGET = perf_monitor
SRCS = main.cpp PerfEvent.cpp
HEADERS = PerfEvent.h utils.h

$(TARGET): $(SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)

