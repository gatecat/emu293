src = $(wildcard *.cpp cpu/*.cpp dma/*.cpp stor/*.cpp sys/*.cpp video/*.cpp)
obj = $(src:.cpp=.o)

CXXFLAGS = -std=c++11 -g  -m32
LDFLAGS += -lSDL2 -m32
all: emu293

emu293: $(obj)
	$(CXX) -o $@ $^ $(LDFLAGS)


.PHONY: clean
clean:
	rm -f $(obj) emu293
