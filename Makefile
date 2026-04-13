# ===== Project =====

TARGET := tg-bot
CC := gcc

# ===== Directories =====

SRC_DIR := src
INC_DIR := include
BUILD_DIR := build

# ===== Sources =====

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# ===== Flags =====

CFLAGS := -Wall -Wextra -std=c11 -O2 -I$(INC_DIR) -D_DEFAULT_SOURCE

# 👉 флаги линковщика (НЕ библиотеки)
LDFLAGS := -Wl,-rpath,'$$ORIGIN'

# 👉 библиотеки (ВСЕГДА в конце)
LDLIBS := -lcurl -lcjson

# ===== Default =====

all: $(TARGET)

# ===== Build =====

$(TARGET): $(OBJS)
	@echo "[LD] $@"
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c $< -o $@

# ===== Run =====

run: $(TARGET)
	LD_LIBRARY_PATH=. ./$(TARGET) -c config/config.conf

# ===== Debug =====

debug: CFLAGS += -g -O0
debug: clean all

# ===== Clean =====

clean:
	@echo "[CLEAN]"
	rm -rf $(BUILD_DIR) $(TARGET)

# ===== Install (optional) =====

install: $(TARGET)
	@echo "[INSTALL]"
	sudo cp $(TARGET) /usr/local/bin/$(TARGET)

uninstall:
	@echo "[UNINSTALL]"
	sudo rm -f /usr/local/bin/$(TARGET)

# ===== Phony =====

.PHONY: all clean run debug install uninstall
