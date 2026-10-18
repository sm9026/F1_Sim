CXX ?= g++
AR ?= ar
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
INCLUDES := -Iinclude -Isrc/data_access

BUILD_DIR := build
DAL_LIB := $(BUILD_DIR)/libf1_sim_dal.a
TEST_BIN := $(BUILD_DIR)/dal_tests

DAL_SRCS := \
	src/data_access/json_parser.cpp \
	src/data_access/dataset_resolver.cpp \
	src/data_access/data_access_layer.cpp

DAL_OBJS := \
	$(BUILD_DIR)/json_parser.o \
	$(BUILD_DIR)/dataset_resolver.o \
	$(BUILD_DIR)/data_access_layer.o

.PHONY: all test clean

all: $(DAL_LIB) $(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/json_parser.o: src/data_access/json_parser.cpp src/data_access/json_parser.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/dataset_resolver.o: src/data_access/dataset_resolver.cpp include/f1_sim/data_access/dataset_resolver.hpp include/f1_sim/data_access/diagnostic.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/data_access_layer.o: src/data_access/data_access_layer.cpp include/f1_sim/data_access/data_access_layer.hpp include/f1_sim/data_access/domain_models.hpp include/f1_sim/data_access/dataset_resolver.hpp include/f1_sim/data_access/diagnostic.hpp src/data_access/json_parser.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(DAL_LIB): $(DAL_OBJS)
	$(AR) rcs $@ $^

$(TEST_BIN): tests/data_access/dal_tests.cpp $(DAL_LIB)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(DAL_LIB) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN) data

clean:
	rm -rf $(BUILD_DIR)
