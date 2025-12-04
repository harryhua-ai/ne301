######################################
# Web Frontend Makefile
######################################
# Inherits configuration from root Makefile (via export)

# Project configuration
TARGET = $(PROJECT_NAME)_Web
BUILD_DIR = build
DIST_DIR = dist
ASSETS_DIR = firmware_assets
SRC_DIR = src

# Package manager
PKG_MGR = pnpm

# Dependency marker
NODE_MODULES = node_modules/.pnpm
PKG_LOCK = pnpm-lock.yaml

######################################
# Default Target
######################################
.PHONY: all
all: $(BUILD_DIR)/$(TARGET).bin

######################################
# Version Synchronization
######################################
# Sync version from version.mk to package.json before building
.PHONY: sync-version
sync-version:
	@echo "Syncing Web version from version.mk..."
	@node scripts/sync_version.cjs

######################################
# Build Targets
######################################
$(BUILD_DIR):
	@mkdir -p $@

# Install dependencies (only if needed)
$(NODE_MODULES): $(PKG_LOCK)
	@echo "Installing dependencies..."
	@$(PKG_MGR) install
	@touch $@

# Build web assets (only if source changed)
# Version sync is done before building
$(DIST_DIR)/index.html: sync-version $(NODE_MODULES) $(shell find $(SRC_DIR) -type f 2>/dev/null)
	@echo "Building Web frontend..."
	@$(PKG_MGR) build
	@touch $@

# Convert to binary (only if dist changed)
$(ASSETS_DIR)/web-assets.bin: $(DIST_DIR)/index.html
	@echo "Converting to binary..."
	@node static_to_c_converter.cjs
	@touch $@

# Final target
$(BUILD_DIR)/$(TARGET).bin: $(ASSETS_DIR)/web-assets.bin | $(BUILD_DIR)
	@cp $< $@
	@echo "Web build complete: $@"

######################################
# Development Targets
######################################
.PHONY: dev
dev: $(NODE_MODULES)
	@echo "Starting development server..."
	@$(PKG_MGR) dev

.PHONY: install
install:
	@echo "Installing dependencies..."
	@$(PKG_MGR) install

.PHONY: build
build: $(BUILD_DIR)/$(TARGET).bin

######################################
# Clean Targets
######################################
.PHONY: clean
clean:
	@echo "Cleaning Web build files..."
	@rm -rf $(BUILD_DIR)/$(TARGET).bin
	@rm -rf $(DIST_DIR)
	@rm -rf $(ASSETS_DIR)

.PHONY: clean-all
clean-all: clean
	@echo "Deep cleaning..."
	@rm -rf node_modules

######################################
# Info
######################################
.PHONY: info
info:
	@echo "Web Frontend Configuration:"
	@echo "  TARGET      = $(TARGET)"
	@echo "  BUILD_DIR   = $(BUILD_DIR)"
	@echo "  PKG_MGR     = $(PKG_MGR)"
	@$(PKG_MGR) --version 2>/dev/null || echo "  pnpm not found"
