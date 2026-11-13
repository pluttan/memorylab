# labos: a UEFI program that runs the memory experiments on bare hardware and draws the graphs.
# Build and QEMU run on macOS (Homebrew llvm@22, lld@22, qemu, ffmpeg) or Linux (clang, lld, qemu, ovmf, ffmpeg).

LLVM_BIN := $(firstword $(wildcard /opt/homebrew/opt/llvm@22/bin) /usr/bin)
LLD_BIN := $(firstword $(wildcard /opt/homebrew/opt/lld@22/bin) /usr/bin)
CLANG ?= $(LLVM_BIN)/clang
LLD_LINK ?= $(LLD_BIN)/lld-link
QEMU ?= qemu-system-x86_64
OVMF ?= $(firstword $(wildcard /opt/homebrew/share/qemu/edk2-x86_64-code.fd /usr/share/OVMF/OVMF_CODE.fd))
PORT ?= 5555
CPU ?= max
HOST ?= 127.0.0.1
LABCTL = $(PY) tools/labctl.py --host $(HOST) --port $(PORT)

VENV := venv
PY := $(VENV)/bin/python3.12
FONT ?= $(HOME)/Library/Fonts/GOSTtypeB.ttf
FONT_SIZE ?= 22

BUILD := build
ESP := $(BUILD)/esp
EFI := $(ESP)/EFI/BOOT/BOOTX64.EFI
SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(SRC))
# h264bsd decodes the films; third_party/libc gives it malloc, memcpy and friends
H264_SRC := $(wildcard third_party/h264bsd/*.c)
H264_OBJ := $(patsubst third_party/h264bsd/%.c,$(BUILD)/h264/%.o,$(H264_SRC))

# The manual and the films lie next to BOOTX64.EFI on the stick
CONTENT := $(ESP)/labos
MANUAL := $(CONTENT)/manual.txt
FILM_NAMES := exp1 exp2 exp3 exp4 exp5 exp6 exp7
FILMS := $(foreach n,$(FILM_NAMES),$(CONTENT)/films/$(n)-720.lfilm $(CONTENT)/films/$(n)-1080.lfilm)
NOTEBOOK := ../lab/memory_experiment.ipynb

# The UEFI ABI is the Microsoft x64 one; no red zone because firmware interrupts use our stack
CFLAGS := -target x86_64-unknown-windows -ffreestanding -fno-builtin -fno-stack-protector -fno-stack-check \
	-mno-stack-arg-probe -mno-red-zone -fshort-wchar -O2 -std=c11 -Wall -Wextra -Werror -Isrc \
	-Ithird_party/h264bsd -Ithird_party/libc
H264_CFLAGS := -target x86_64-unknown-windows -ffreestanding -fno-builtin -fno-stack-protector -fno-stack-check \
	-mno-stack-arg-probe -mno-red-zone -O2 -std=c99 -w -D_ERROR_PRINT -Ithird_party/libc -Ithird_party/h264bsd
# make build SYNTHETIC=1: experiment 1 takes its times from a DRAM model, to check the drawing in QEMU
ifeq ($(SYNTHETIC),1)
CFLAGS += -DROWCONF_SYNTHETIC
endif
LDFLAGS := /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:x64

# Absolute paths: with -daemonize QEMU changes to / and opens the FAT directory lazily
ABS := $(abspath $(BUILD))
QEMU_ARGS := -machine q35 -cpu $(CPU) -m 1G -smp 1 \
	-drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
	-drive format=raw,file=fat:rw:$(ABS)/esp \
	-netdev user,id=net0,hostfwd=udp:127.0.0.1:$(PORT)-:5555 -device e1000,netdev=net0 \
	-serial file:$(ABS)/serial.log

# Pure modules tested on this machine: the test name, then the sources it needs
TESTS := plotmath:src/plotmath.c,src/fmt.c netpkt:src/netpkt.c analysis:src/analysis.c params:src/params.c \
	leaf2:src/cpuid_leaf2.c film:src/film.c manualfmt:src/manualfmt.c

.PHONY: all build font test qemu qemu-headless screenshot stop clean help ping remote-log remote-screen usb \
	manual films content filmbench

all: build

help:
	@echo "make build          - собрать $(EFI)"
	@echo "make manual         - методичка из тетради memorylab в $(MANUAL)"
	@echo "make films          - ролики опытов для labos (720p и 1080p, несколько минут)"
	@echo "make content        - методичка и ролики"
	@echo "make qemu           - запустить в QEMU с окном"
	@echo "make qemu-headless  - запустить в QEMU без окна (журнал: $(BUILD)/serial.log)"
	@echo "make screenshot     - снимок экрана QEMU в $(BUILD)/screen.png"
	@echo "make stop           - остановить QEMU без окна"
	@echo "make test           - тесты чистых модулей на этой машине"
	@echo "make filmbench FILM=build/esp/labos/films/exp1-1080.lfilm - скорость декодера на этой машине"
	@echo "make font           - пересобрать src/font_gost.h из $(FONT)"
	@echo "make ping           - спросить labos по сети (HOST=$(HOST) PORT=$(PORT))"
	@echo "make remote-log     - журнал labos по сети"
	@echo "make remote-screen  - снимок экрана labos по сети в $(BUILD)/remote.png"
	@echo "make usb VOLUME=/Volumes/LABOS - записать labos, методичку и ролики на флешку FAT32"

build: $(EFI)

$(BUILD)/obj/%.o: src/%.c $(wildcard src/*.h) | $(BUILD)/obj
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD)/h264/%.o: third_party/h264bsd/%.c | $(BUILD)/h264
	$(CLANG) $(H264_CFLAGS) -c $< -o $@

$(EFI): $(OBJ) $(H264_OBJ) | $(ESP)/EFI/BOOT
	$(LLD_LINK) $(LDFLAGS) /out:$@ $(OBJ) $(H264_OBJ)

$(BUILD)/obj $(BUILD)/h264 $(ESP)/EFI/BOOT:
	mkdir -p $@

manual: $(MANUAL)

$(MANUAL): $(NOTEBOOK) $(wildcard manual/*.md) tools/mkmanual.py src/font_gost.h | $(VENV)
	$(PY) tools/mkmanual.py $(NOTEBOOK) manual src/font_gost.h $@

films: $(FILMS)

$(CONTENT)/films/%-720.lfilm: ../lab/films/%.mp4 tools/mkfilm.py | $(VENV)
	nice -n 10 $(PY) tools/mkfilm.py $< $@ 720

$(CONTENT)/films/%-1080.lfilm: ../lab/films/%.mp4 tools/mkfilm.py | $(VENV)
	nice -n 10 $(PY) tools/mkfilm.py $< $@ 1080

content: manual films

FILM ?= $(CONTENT)/films/exp1-1080.lfilm

filmbench:
	@mkdir -p $(BUILD)
	cc -O2 -w -Isrc -Ithird_party/h264bsd tools/filmbench.c src/film.c $(H264_SRC) -o $(BUILD)/filmbench
	$(BUILD)/filmbench $(FILM)

$(VENV):
	python3.12 -m venv $(VENV) && $(VENV)/bin/pip install -q -U pip && $(VENV)/bin/pip install -q -r requirements.txt

font: $(VENV)
	$(PY) tools/mkfont.py $(FONT) $(FONT_SIZE) src/font_gost.h

test:
	@mkdir -p $(BUILD); set -e; for t in $(TESTS); do \
		name=$${t%%:*}; sources=$$(echo $${t#*:} | tr , ' '); \
		cc -O1 -Wall -Wextra -Werror -Isrc tests/test_$$name.c $$sources -o $(BUILD)/test_$$name; \
		printf '%-10s ' $$name; $(BUILD)/test_$$name; \
	done

qemu: build manual
	$(QEMU) $(QEMU_ARGS)

qemu-headless: build manual
	$(QEMU) $(QEMU_ARGS) -display none -monitor unix:$(ABS)/monitor.sock,server,nowait \
		-daemonize -pidfile $(ABS)/qemu.pid

screenshot: $(VENV)
	echo "screendump $(ABS)/screen.ppm" | nc -U -w 1 $(BUILD)/monitor.sock > /dev/null
	sleep 1
	$(PY) -c "from PIL import Image; Image.open('$(BUILD)/screen.ppm').save('$(BUILD)/screen.png')"

stop:
	-[ -f $(BUILD)/qemu.pid ] && kill $$(cat $(BUILD)/qemu.pid) && rm -f $(BUILD)/qemu.pid

ping: $(VENV)
	$(LABCTL) ping

remote-log: $(VENV)
	$(LABCTL) log

remote-screen: $(VENV)
	mkdir -p $(BUILD)
	$(LABCTL) screen $(BUILD)/remote.png

usb: build content
	@[ -n "$(VOLUME)" ] && [ -d "$(VOLUME)" ] || { echo "Укажите флешку FAT32: make usb VOLUME=/Volumes/LABOS"; exit 1; }
	mkdir -p "$(VOLUME)/EFI/BOOT" "$(VOLUME)/labos/films"
	cp $(EFI) "$(VOLUME)/EFI/BOOT/BOOTX64.EFI"
	cp $(MANUAL) "$(VOLUME)/labos/manual.txt"
	cp $(FILMS) "$(VOLUME)/labos/films/"
	@echo "Готово: labos, методичка и $(words $(FILMS)) роликов на $(VOLUME)"

clean:
	rm -rf $(BUILD)
