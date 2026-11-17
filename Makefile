# ==============================================================================
# ARDUINO-CLI MAKEFILE (Arduino UNO R4 WiFi)
# ==============================================================================
#
# Usage:
#   make build                        # compile the default sketch (CAN_OBD_LCD)
#   make upload PORT=/dev/ttyACM0     # compile and upload
#   make monitor PORT=/dev/ttyACM0    # open a serial monitor
#   make flash PORT=/dev/ttyACM0      # build + upload + monitor
#   make build SKETCH=GC9A01_round_display   # target a different sketch folder
#
# ==============================================================================

# --- Project Configuration ---
ARDUINO_CLI   ?= arduino-cli
FQBN          ?= arduino:renesas_uno:unor4wifi
SKETCH        ?= CAN_OBD_LCD
SKETCH_DIR    := $(CURDIR)/$(SKETCH)
BUILD_DIR     := $(SKETCH_DIR)/build
PORT          ?= /dev/ttyACM0
BAUD_RATE     ?= 115200

FQBN_FLAGS    := --fqbn $(FQBN)

# --- Default Goal ---
.DEFAULT_GOAL := help

.PHONY: all build upload monitor flash clean list help

all: build ## Alias for 'build'

build: ## Compile the sketch (override with SKETCH=<folder>)
	@echo "🛠️  Compiling $(SKETCH)..."
	$(ARDUINO_CLI) compile $(FQBN_FLAGS) --output-dir $(BUILD_DIR) $(SKETCH_DIR)

upload: build ## Compile, then upload over serial (override with PORT=<port>)
	@echo "🚀 Uploading $(SKETCH) to $(PORT)..."
	$(ARDUINO_CLI) upload -p $(PORT) $(FQBN_FLAGS) --input-dir $(BUILD_DIR) $(SKETCH_DIR)

monitor: ## Open a serial monitor session (override with PORT=<port>)
	@echo "📺 Opening serial monitor on $(PORT) at $(BAUD_RATE) baud..."
	$(ARDUINO_CLI) monitor -p $(PORT) --config baudrate=$(BAUD_RATE)

flash: upload monitor ## Build, upload, then open the serial monitor

clean: ## Remove build artifacts for the current sketch
	@echo "🧹 Cleaning $(BUILD_DIR)..."
	rm -rf $(BUILD_DIR)

list: ## List connected boards and their ports
	$(ARDUINO_CLI) board list

help: ## Show this help message
	@echo "📋 Available targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2}'
