# PS5 Overlay Makefile
# Builds the standalone PS5 Overlay (Payload + Injector) using ps5-payload-sdk

ifndef PS5_PAYLOAD_SDK
    ifneq ($(wildcard $(HOME)/ps5-payload-sdk/toolchain/prospero.mk),)
        PS5_PAYLOAD_SDK := $(HOME)/ps5-payload-sdk
    else ifneq ($(wildcard /home/kus/ps5-payload-sdk/toolchain/prospero.mk),)
        PS5_PAYLOAD_SDK := /home/kus/ps5-payload-sdk
    else ifneq ($(wildcard /opt/ps5-payload-sdk/toolchain/prospero.mk),)
        PS5_PAYLOAD_SDK := /opt/ps5-payload-sdk
    endif
endif

PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

BIN_DIR     := bin
PAYLOAD_ELF := $(BIN_DIR)/overlay_payload.elf
VERSION     ?= v1.0.19
LOADER_ELF  := $(BIN_DIR)/ps5-overlay-kus_$(VERSION).elf

# Flags
PAYLOAD_CXXFLAGS := -target x86_64-sie-ps5 -march=znver2 -fPIC -fPIE -O2 -Wall -Werror \
                    -Wno-unused-function -Wno-deprecated -Wno-unused-command-line-argument \
                    -nostartfiles -Ipayload -Icommon
LOADER_CFLAGS    := -target x86_64-sie-ps5 -O2 -Wall -Werror -Wno-unused-function \
                    -Iloader -Icommon

# Source files
PAYLOAD_SRCS := payload/mono_runtime.cpp \
                payload/main.cpp \
                payload/config.cpp \
                payload/web_server.cpp \
                payload/payload_log.cpp \
                common/nid.c

LOADER_SRCS  := loader/main.c \
                loader/proc.c \
                loader/ucred.c \
                loader/injector.c \
                loader/pt.c \
                loader/elfldr.c \
                loader/notify.c \
                loader/loader_log.c \
                loader/embedded_payload.S

.PHONY: all clean test deploy

all: $(LOADER_ELF)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Step 1: Build the injected payload ELF
$(PAYLOAD_ELF): $(PAYLOAD_SRCS) | $(BIN_DIR)
	@echo "==> Building Overlay Payload ELF..."
	$(CXX) $(PAYLOAD_CXXFLAGS) $^ -lkernel_sys -o $@

# Step 2: Build the loader ELF (which embeds overlay_payload.elf via .incbin)
$(LOADER_ELF): $(LOADER_SRCS) $(PAYLOAD_ELF) | $(BIN_DIR)
	@echo "==> Building PS5 Overlay Loader ELF (embedding payload)..."
	$(CC) $(LOADER_CFLAGS) $(LOADER_SRCS) -lkernel_sys -o $@
	@echo "==> Build complete: $@"

clean:
	@echo "==> Cleaning build artifacts..."
	rm -rf $(BIN_DIR)/*.elf $(BIN_DIR)/*.o

test: deploy

deploy: $(LOADER_ELF)
	@echo "==> Deploying $(LOADER_ELF) to $(PS5_HOST):$(PS5_PORT)..."
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $(LOADER_ELF)
