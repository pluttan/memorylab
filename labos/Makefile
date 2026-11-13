# labos: a UEFI program that runs the memory experiments on bare hardware and draws the graphs.
# Build and QEMU run on macOS (Homebrew llvm@22, lld@22, qemu) or Linux (clang, lld, qemu, ovmf).

LLVM_BIN := $(firstword $(wildcard /opt/homebrew/opt/llvm@22/bin) /usr/bin)
LLD_BIN := $(firstword $(wildcard /opt/homebrew/opt/lld@22/bin) /usr/bin)
CLANG ?= $(LLVM_BIN)/clang
LLD_LINK ?= $(LLD_BIN)/lld-link
QEMU ?= qemu-system-x86_64
OVMF ?= $(firstword $(wildcard /opt/homebrew/share/qemu/edk2-x86_64-code.fd /usr/share/OVMF/OVMF_CODE.fd))
PORT ?= 5555

VENV := venv
PY := $(VENV)/bin/python3.12
FONT ?= $(HOME)/Library/Fonts/GOSTtypeB.ttf
FONT_SIZE ?= 22

BUILD := build
ESP := $(BUILD)/esp
EFI := $(ESP)/EFI/BOOT/BOOTX64.EFI
SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(SRC))

# The UEFI ABI is the Microsoft x64 one; no red zone because firmware interrupts use our stack
CFLAGS := -target x86_64-unknown-windows -ffreestanding -fno-builtin -fno-stack-protector -fno-stack-check \
	-mno-stack-arg-probe -mno-red-zone -fshort-wchar -O2 -std=c11 -Wall -Wextra -Werror -Isrc
LDFLAGS := /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:x64

# Absolute paths: with -daemonize QEMU changes to / and opens the FAT directory lazily
ABS := $(abspath $(BUILD))
QEMU_ARGS := -machine q35 -cpu max -m 1G -smp 1 \
	-drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
	-drive format=raw,file=fat:rw:$(ABS)/esp \
	-netdev user,id=net0,hostfwd=udp:127.0.0.1:$(PORT)-:5555 -device e1000,netdev=net0 \
	-serial file:$(ABS)/serial.log

.PHONY: all build venv font test qemu qemu-headless screenshot stop clean help

all: build

help:
	@echo "make build          - собрать $(EFI)"
	@echo "make qemu           - запустить в QEMU с окном"
	@echo "make qemu-headless  - запустить в QEMU без окна (журнал: $(BUILD)/serial.log)"
	@echo "make screenshot     - снимок экрана QEMU в $(BUILD)/screen.png"
	@echo "make stop           - остановить QEMU без окна"
	@echo "make test           - тесты чистых модулей на этой машине"
	@echo "make font           - пересобрать src/font_gost.h из $(FONT)"

build: $(EFI)

$(BUILD)/obj/%.o: src/%.c $(wildcard src/*.h) | $(BUILD)/obj
	$(CLANG) $(CFLAGS) -c $< -o $@

$(EFI): $(OBJ) | $(ESP)/EFI/BOOT
	$(LLD_LINK) $(LDFLAGS) /out:$@ $(OBJ)

$(BUILD)/obj $(ESP)/EFI/BOOT:
	mkdir -p $@

$(VENV):
	python3.12 -m venv $(VENV) && $(VENV)/bin/pip install -q -U pip && $(VENV)/bin/pip install -q -r requirements.txt

venv: $(VENV)

font: $(VENV)
	$(PY) tools/mkfont.py $(FONT) $(FONT_SIZE) src/font_gost.h

test: | $(BUILD)
	cc -O1 -Wall -Wextra -Werror -Isrc tests/test_plotmath.c src/plotmath.c src/fmt.c -o $(BUILD)/test_plotmath
	$(BUILD)/test_plotmath

$(BUILD):
	mkdir -p $@

qemu: build
	$(QEMU) $(QEMU_ARGS)

qemu-headless: build
	$(QEMU) $(QEMU_ARGS) -display none -monitor unix:$(ABS)/monitor.sock,server,nowait \
		-daemonize -pidfile $(ABS)/qemu.pid

screenshot: $(VENV)
	echo "screendump $(ABS)/screen.ppm" | nc -U -w 1 $(BUILD)/monitor.sock > /dev/null
	sleep 1
	$(PY) -c "from PIL import Image; Image.open('$(BUILD)/screen.ppm').save('$(BUILD)/screen.png')"

stop:
	-[ -f $(BUILD)/qemu.pid ] && kill $$(cat $(BUILD)/qemu.pid) && rm -f $(BUILD)/qemu.pid

clean:
	rm -rf $(BUILD)
