TARGET_NAME = pixy-i2c-test
CPPFLAGS = -std=c++11 -g
LDFLAGS =
SOURCES := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp,%.o,$(SOURCES))

ifeq ($(DEBUG),1)
  CPPFLAGS += -DDEBUG -Og -g
endif

all: $(TARGET_NAME)

$(TARGET_NAME): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(CPPFLAGS) $(LDFLAGS)

%.o: %.cpp
	@$(CXX) $(CFLAGS) -c $<

.PHONY: clean all
clean:
	@rm -f *.o $(TARGET_NAME)
