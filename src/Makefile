src = $(wildcard *.cpp cpu/*.cpp dma/*.cpp stor/*.cpp sys/*.cpp video/*.cpp io/*.cpp audio/*.cpp)
obj = $(src:.cpp=.o)

CXXFLAGS = -std=c++11 -g -O3 `wx-config --cxxflags`
LDFLAGS += -lSDL2 -lz -lv4l2 -lv4lconvert `wx-config --libs`
all: emu293

emu293: $(obj)
	$(CXX) -Wall -o $@ $^ $(LDFLAGS)


.PHONY: clean
clean:
	rm -f $(obj) emu293
