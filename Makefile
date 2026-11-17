CC       ?= gcc
INC_DIR  := include
SRC_DIR  := src
TEST_DIR := tests
OUT_DIR  := tests/out

RULE_FLAGS := -std=c99 -pedantic-errors -Wall -Wextra -Werror \
              -Wconversion -Wsign-conversion -Wshadow \
              -Wstrict-prototypes -Wmissing-prototypes -Wundef \
              -Wcast-qual -Wcast-align=strict -Wvla \
              -Wstack-usage=256 -fstack-usage -fno-common
TEST_FLAGS := -std=c99 -Wall -Wextra -O0 -g -I$(INC_DIR) -I$(TEST_DIR)

.PHONY: all static-check test coverage clean

all: static-check test

static-check:
	mkdir -p $(OUT_DIR)
	$(CC) $(RULE_FLAGS) -O1 -fno-inline -I$(INC_DIR) -c $(SRC_DIR)/axi_iic_hal.c -o $(OUT_DIR)/rules_release_axi_iic_hal.o
	$(CC) $(RULE_FLAGS) -O1 -fno-inline -I$(INC_DIR) -c $(SRC_DIR)/adt7410_driver.c -o $(OUT_DIR)/rules_release_adt7410_driver.o
	$(CC) $(RULE_FLAGS) -O1 -fno-inline -I$(INC_DIR) -c $(SRC_DIR)/main.c -o $(OUT_DIR)/rules_release_main.o
	$(CC) $(RULE_FLAGS) -O1 -fno-inline -DDEBUG_BUILD -I$(INC_DIR) -c $(SRC_DIR)/axi_iic_hal.c -o $(OUT_DIR)/rules_debug_axi_iic_hal.o
	$(CC) $(RULE_FLAGS) -O1 -fno-inline -DDEBUG_BUILD -I$(INC_DIR) -c $(SRC_DIR)/adt7410_driver.c -o $(OUT_DIR)/rules_debug_adt7410_driver.o
	$(CC) $(RULE_FLAGS) -O1 -fno-inline -DDEBUG_BUILD -I$(INC_DIR) -c $(SRC_DIR)/main.c -o $(OUT_DIR)/rules_debug_main.o

test:
	mkdir -p $(OUT_DIR)
	$(CC) $(TEST_FLAGS) --coverage -c $(SRC_DIR)/axi_iic_hal.c -o $(OUT_DIR)/axi_iic_hal.o
	$(CC) $(TEST_FLAGS) --coverage -c $(SRC_DIR)/adt7410_driver.c -o $(OUT_DIR)/adt7410_driver.o
	$(CC) $(TEST_FLAGS) -c $(TEST_DIR)/mock_axi_iic.c -o $(OUT_DIR)/mock_axi_iic.o
	$(CC) $(TEST_FLAGS) --coverage $(TEST_DIR)/test_adt7410_driver.c $(OUT_DIR)/adt7410_driver.o $(OUT_DIR)/mock_axi_iic.o -lm -o $(OUT_DIR)/test_adt7410_driver
	$(CC) $(TEST_FLAGS) --coverage $(TEST_DIR)/test_axi_iic_hal.c $(OUT_DIR)/axi_iic_hal.o -o $(OUT_DIR)/test_axi_iic_hal
	$(CC) $(TEST_FLAGS) --coverage $(TEST_DIR)/test_integration.c $(OUT_DIR)/adt7410_driver.o $(OUT_DIR)/axi_iic_hal.o -o $(OUT_DIR)/test_integration
	$(CC) $(TEST_FLAGS) --coverage -Dmain=app_main -c $(SRC_DIR)/main.c -o $(OUT_DIR)/main.o
	$(CC) $(TEST_FLAGS) --coverage $(TEST_DIR)/test_app.c $(OUT_DIR)/main.o $(OUT_DIR)/adt7410_driver.o $(OUT_DIR)/mock_axi_iic.o -o $(OUT_DIR)/test_app
	$(OUT_DIR)/test_adt7410_driver
	$(OUT_DIR)/test_axi_iic_hal
	$(OUT_DIR)/test_integration
	$(OUT_DIR)/test_app

coverage: test
	gcov -b -c -o $(OUT_DIR) $(SRC_DIR)/axi_iic_hal.c
	gcov -b -c -o $(OUT_DIR) $(SRC_DIR)/adt7410_driver.c
	gcov -b -c -o $(OUT_DIR) $(SRC_DIR)/main.c

clean:
	rm -rf $(OUT_DIR) build *.gcov
