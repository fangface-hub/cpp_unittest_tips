CXX ?= g++
CXXSTD ?= c++17
DATASET_DIR ?= third_party/CppDatasetUtil
DATASET_LIB := $(DATASET_DIR)/build/libcppdatasetutil.a
BUILD := build

CPPFLAGS += -Isrc -Itest -I$(DATASET_DIR)/Include
CXXFLAGS ?= -O0 -g -Wall -Wextra

# The SUT is built with the rename/public macros and coverage instrumentation.
# The test and the library are not instrumented, so coverage covers only the SUT.
SUT_OBJ := $(BUILD)/SUT.o
TEST_BIN := $(BUILD)/test_sut

.PHONY: all test coverage clean

all: $(TEST_BIN)

$(DATASET_LIB):
	git submodule update --init $(DATASET_DIR)
	$(MAKE) -C $(DATASET_DIR) static

$(BUILD):
	mkdir -p $@

$(SUT_OBJ): src/SUT.cpp src/SUT.h test/SUTStub.h | $(BUILD) $(DATASET_LIB)
	$(CXX) -std=$(CXXSTD) $(CPPFLAGS) $(CXXFLAGS) --coverage \
		-DSUT_UNDER_TEST --include SUTStub.h -c $< -o $@

$(TEST_BIN): test/test_sut.cpp test/SUTStub.h $(SUT_OBJ) $(DATASET_LIB)
	$(CXX) -std=$(CXXSTD) $(CPPFLAGS) $(CXXFLAGS) --coverage \
		--include SUTStub.h test/test_sut.cpp $(SUT_OBJ) $(DATASET_LIB) -o $@

test: $(TEST_BIN)
	rm -f $(BUILD)/*.gcda
	./$(TEST_BIN)

# Fails unless line coverage of src/SUT.cpp is 100%.
# Branch figures are printed for reference only: they include compiler-generated
# exception and std::string edges that cannot be exercised from the test.
coverage: test
	cd $(BUILD) && gcov -b -c SUT.o > gcov.txt && cat gcov.txt
	@cd $(BUILD) && awk "/^File 'src\/SUT.cpp'/{f=1;next} f&&/^Lines executed/{print; exit !(\$$2==\"executed:100.00%\")}" gcov.txt \
		&& echo "OK: 100% line coverage" || { echo "FAIL: line coverage below 100%"; exit 1; }

.PHONY: clean
clean:
	rm -rf $(BUILD)
