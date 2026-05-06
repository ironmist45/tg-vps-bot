# ===== Project =====
TARGET := tg-bot
CC := gcc

# ===== Directories =====
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build

# ===== Sources =====
SRCS := $(wildcard $(SRC_DIR)/*.c) \
		$(wildcard $(SRC_DIR)/commands/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# ===== Flags =====
CFLAGS := -Wall -Wextra -std=c11 -O2 -I$(INC_DIR) -D_DEFAULT_SOURCE
LDFLAGS ?=

# 🔥 статические библиотеки (ВАЖНО: порядок!)
# LDLIBS := -lssl -lcrypto -lz -lpthread

# ===== Default =====
all: $(TARGET)

# ===== Build =====
$(TARGET): $(OBJS)
	@echo "[LD] $@ (custom LDFLAGS)"
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

# ===== Special flags for SSE4.2 modules =====
$(BUILD_DIR)/logstat.o: $(SRC_DIR)/logstat.c
	@mkdir -p $(dir $@)
	@echo "[CC] $< (SSE4.2)"
	$(CC) $(CFLAGS) -msse4.2 -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c $< -o $@

# ===== Run =====
run: $(TARGET)
	./$(TARGET) -c config/config.conf

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
