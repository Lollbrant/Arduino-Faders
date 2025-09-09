CC = gcc
INCLUDES = -I/usr/include/pipewire-0.3 -I/usr/include/spa-0.2 -I./libraries/cJSON -I./libraries/uthash
CONFIG_DIR = "/etc/HyperControl"

CFLAGS = -Wall -Wextra -O2 $(INCLUDES) -D_REENTRANT -lpipewire-0.3

TARGET = bin/HyperFader

SRC = pipewire_client.c libraries/cJSON/cJSON.c

all: $(TARGET)
$(TARGET): $(SRC)
	@echo "\033[1;32m[BUILD]\033[0m Compiling..."
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
	@echo "\033[1;32m[SUCCESS]\033[0m Build complete!"

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

config:
	@mkdir -p $(CONFIG_DIR)
	@cp HyperFader.json $(CONFIG_DIR)/HyperFader.json
	@echo "Default config file '$(CONFIG_DIR)/HyperFader.json' created."

help:
	@echo "Available targets:"
	@echo "  make            - Build the project"
	@echo "  make clean      - Remove compiled files"
	@echo "  make run        - Build and run the project"
	@echo "  make install    - Install the binary system-wide"
	@echo "  make config     - Generate a default config"

.PHONY: clean config help
