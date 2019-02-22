CXX       	 := clang++
CXXFLAGS 	   := -std=c++11 -g -Wall
SRC_DIR      := ./src
MACRO        := DEBUG
TEST_DIR     := ./test
TARGET       := g-profiler
SRC          := $(SRC_DIR)/profiler.cc 

OBJECTS      := $(SRC:%.cpp=$(OBJ_DIR)/%.o)

.PHONY: all test clean 
	
all: $(TARGET)

$(OBJ_DIR)/%.o: %.cc
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ -c $< 

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) 

clean:
	-@rm -rf $(TARGET)
	-@rm -rf *.out

