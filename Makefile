CXX ?= g++
AR ?= ar
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
INCLUDES := -Iinclude -Isrc/data_access

BUILD_DIR := build
DAL_LIB := $(BUILD_DIR)/libf1_sim_dal.a
SIM_LIB := $(BUILD_DIR)/libf1_sim_engine.a
APP_BIN := $(BUILD_DIR)/f1_sim
DAL_TEST_BIN := $(BUILD_DIR)/dal_tests
SIM_TEST_BIN := $(BUILD_DIR)/simulation_tests

DAL_OBJS := \
	$(BUILD_DIR)/json_parser.o \
	$(BUILD_DIR)/dataset_resolver.o \
	$(BUILD_DIR)/data_access_layer.o

SIM_OBJS := \
	$(BUILD_DIR)/lap_evaluator.o \
	$(BUILD_DIR)/tyre_inventory.o \
	$(BUILD_DIR)/strategy_validator.o \
	$(BUILD_DIR)/race_session.o

.PHONY: all test run clean

all: $(DAL_LIB) $(SIM_LIB) $(APP_BIN) $(DAL_TEST_BIN) $(SIM_TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/json_parser.o: src/data_access/json_parser.cpp src/data_access/json_parser.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/dataset_resolver.o: src/data_access/dataset_resolver.cpp include/f1_sim/data_access/dataset_resolver.hpp include/f1_sim/data_access/diagnostic.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/data_access_layer.o: src/data_access/data_access_layer.cpp include/f1_sim/data_access/data_access_layer.hpp include/f1_sim/data_access/domain_models.hpp include/f1_sim/data_access/dataset_resolver.hpp include/f1_sim/data_access/diagnostic.hpp src/data_access/json_parser.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/lap_evaluator.o: src/simulation/lap_evaluator.cpp include/f1_sim/simulation/lap_evaluator.hpp include/f1_sim/simulation/engine_types.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/tyre_inventory.o: src/simulation/tyre_inventory.cpp include/f1_sim/simulation/tyre_inventory.hpp include/f1_sim/simulation/lap_evaluator.hpp include/f1_sim/simulation/engine_types.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/strategy_validator.o: src/simulation/strategy_validator.cpp include/f1_sim/simulation/strategy_validator.hpp include/f1_sim/simulation/tyre_inventory.hpp include/f1_sim/simulation/engine_types.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/race_session.o: src/simulation/race_session.cpp include/f1_sim/simulation/race_session.hpp include/f1_sim/simulation/strategy_validator.hpp include/f1_sim/simulation/tyre_inventory.hpp include/f1_sim/simulation/lap_evaluator.hpp include/f1_sim/simulation/engine_types.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(DAL_LIB): $(DAL_OBJS)
	$(AR) rcs $@ $^

$(SIM_LIB): $(SIM_OBJS)
	$(AR) rcs $@ $^

$(APP_BIN): src/main.cpp $(SIM_LIB) $(DAL_LIB)
	$(CXX) $(CXXFLAGS) -Iinclude $< $(SIM_LIB) $(DAL_LIB) -o $@

$(DAL_TEST_BIN): tests/data_access/dal_tests.cpp $(DAL_LIB)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(DAL_LIB) -o $@

$(SIM_TEST_BIN): tests/simulation/simulation_tests.cpp $(SIM_LIB) $(DAL_LIB)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(SIM_LIB) $(DAL_LIB) -o $@

run: $(APP_BIN)
	./$(APP_BIN) --data-root data

test: $(DAL_TEST_BIN) $(SIM_TEST_BIN)
	./$(DAL_TEST_BIN) data
	./$(SIM_TEST_BIN) data

clean:
	rm -rf $(BUILD_DIR)
