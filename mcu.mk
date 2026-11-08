# Микроконтроллер: прошивка hardware-mc (PlatformIO) и UART-WebSocket мост.
# Основной Makefile этот файл не подключает, поэтому make all МК не трогает.
# Запуск: make -f mcu.mk <цель>, список целей: make -f mcu.mk help

VENV_DIR = .venv
PYTHON = $(VENV_DIR)/bin/python
PIP = $(VENV_DIR)/bin/pip
PRETTY = $(PYTHON) -m iu6hardwarememorylab.pretty_print
PRETTY_RAW = python3 iu6hardwarememorylab/pretty_print.py

LOG_DIR = log
IMG_DIR = report/img
TEST_DIR = lab
SERVER_LOG = $(LOG_DIR)/server.log
JUPYTER_LOG = $(LOG_DIR)/jupyter.log
JUPYTER_PORT = 8888

HARDWARE_MC_DIR = hardware-mc
PIO_SELECT = $(PYTHON) -m iu6hardwarememorylab.pio_select
MCU_PORT_FILE = .mcu_port
MCU_DEPS = $(VENV_DIR)/.mcu_deps_installed

LINE = ========================================

.PHONY: help all deps server-mcu mcu-run pio-install pio pio-build pio-upload pio-monitor \
        pio-quick pio-quick-upload pio-clean pio-boards clean stop-desktop

help:
	@echo "$(LINE)"
	@echo " Memory Lab - микроконтроллер (make -f mcu.mk ...)"
	@echo "$(LINE)"
	@echo " all              - Прошить МК, запустить UART-сервер и Jupyter Lab"
	@echo " server-mcu       - UART-WebSocket сервер для МК"
	@echo " mcu-run          - UART-сервер в фоне + Jupyter Lab"
	@echo " pio              - Интерактивный выбор МК + сборка + загрузка"
	@echo " pio-build        - Интерактивный выбор + сборка"
	@echo " pio-upload       - Интерактивный выбор + загрузка"
	@echo " pio-boards       - Показать список плат"
	@echo " pio-monitor      - UART монитор"
	@echo " pio-quick PIO_ENV=esp32        - Сборка без меню"
	@echo " pio-quick-upload PIO_ENV=esp32 - Загрузка без меню"
	@echo " clean            - Удалить сборку PlatformIO и сохранённый порт"
	@echo "$(LINE)"

$(LOG_DIR) $(IMG_DIR):
	@mkdir -p $@

# Окружение основного проекта плюс pyserial для UART
$(MCU_DEPS):
	@$(MAKE) --no-print-directory -f Makefile deps
	@$(PIP) install pyserial --progress-bar off
	@touch $@

deps: $(MCU_DEPS)

# Desktop-сервер занимает тот же порт, что и UART-мост
stop-desktop:
	@$(MAKE) --no-print-directory -f Makefile kill-server

# Запустить WebSocket сервер для МК (через UART)
server-mcu: deps stop-desktop $(LOG_DIR)
	@$(PYTHON) -m iu6hardwarememorylab.uart_server

# Запустить сервер МК в фоне и открыть lab
mcu-run: deps stop-desktop $(LOG_DIR) $(IMG_DIR)
	@$(PRETTY) header "Запуск UART-WebSocket сервера для МК"
	@$(PYTHON) -m iu6hardwarememorylab.uart_server > $(SERVER_LOG) 2>&1 &
	@sleep 3
	@$(PRETTY) success "Сервер запущен"
	@$(PRETTY) info "Log: $(SERVER_LOG)"
	@$(PRETTY) header "Jupyter Lab"
	@$(PRETTY) success "URL: http://localhost:$(JUPYTER_PORT)"
	@cd $(TEST_DIR) && ./../$(PYTHON) -m jupyter lab --port=$(JUPYTER_PORT) > ../$(JUPYTER_LOG) 2>&1

# Прошить МК, запустить UART-сервер на сохранённом порту и открыть lab
all: deps pio-install stop-desktop $(LOG_DIR) $(IMG_DIR)
	@$(PRETTY) header "Программирование микроконтроллера"
	@$(PRETTY) info "Сейчас будет предложен выбор платы и порта..."
	@$(PYTHON) -c "\
import sys; \
sys.path.insert(0, '.'); \
from iu6hardwarememorylab.pio_select import select_environment, get_serial_ports, select_port, run_build, run_upload; \
from pathlib import Path; \
hw_dir = Path('$(HARDWARE_MC_DIR)'); \
env = select_environment(); \
ret = run_build(env, hw_dir); \
if ret != 0: sys.exit(ret); \
ports = get_serial_ports(); \
port = select_port(ports); \
if not port: sys.exit(1); \
ret = run_upload(env, port, hw_dir); \
if ret != 0: sys.exit(ret); \
Path('$(MCU_PORT_FILE)').write_text(port); \
print(f'Порт сохранён: {port}')"
	@$(PRETTY) success "Микроконтроллер запрограммирован!"
	@MCU_PORT=$$(cat $(MCU_PORT_FILE) 2>/dev/null || echo ""); \
	if [ -n "$$MCU_PORT" ]; then \
		$(PRETTY) info "Запуск UART-сервера на порту $$MCU_PORT..."; \
		$(PYTHON) -m iu6hardwarememorylab.uart_server -p "$$MCU_PORT" > $(SERVER_LOG) 2>&1 & \
		sleep 2; \
		$(PRETTY) success "UART-сервер запущен!"; \
	else \
		$(PRETTY) error "Порт МК не найден, UART-сервер не запущен"; \
	fi
	@$(PRETTY) success "URL: http://localhost:$(JUPYTER_PORT)"
	@cd $(TEST_DIR) && ./../$(PYTHON) -m jupyter lab --port=$(JUPYTER_PORT) > ../$(JUPYTER_LOG) 2>&1

# Проверить и установить PlatformIO CLI (через pipx для изоляции)
pio-install:
	@$(PRETTY_RAW) header "Установка PlatformIO CLI"
	@if command -v pio >/dev/null 2>&1; then \
		$(PRETTY_RAW) success "PlatformIO уже установлен: $$(pio --version)"; \
	else \
		$(PRETTY_RAW) info "Установка через pipx..."; \
		if ! command -v pipx >/dev/null 2>&1; then \
			$(PRETTY_RAW) info "Установка pipx..."; \
			python3 -m pip install --user pipx; \
			python3 -m pipx ensurepath; \
		fi; \
		pipx install platformio; \
		$(PRETTY_RAW) success "PlatformIO установлен!"; \
		$(PRETTY_RAW) info "Перезапустите терминал или выполните: source ~/.bashrc"; \
	fi

# Интерактивный выбор МК + сборка + загрузка + монитор
pio: deps pio-install
	@$(PIO_SELECT) all

# Интерактивный выбор и сборка прошивки
pio-build: deps pio-install
	@$(PIO_SELECT) build

# Интерактивный выбор и загрузка прошивки
pio-upload: deps pio-install
	@$(PIO_SELECT) upload

# UART монитор
pio-monitor: pio-install
	@$(PRETTY_RAW) header "UART Monitor (115200 baud)"
	@cd $(HARDWARE_MC_DIR) && pio device monitor -b 115200

# Быстрая сборка без интерактивного меню (с указанием PIO_ENV)
pio-quick: pio-install
	@if [ -z "$(PIO_ENV)" ]; then \
		$(PRETTY_RAW) error "Укажите PIO_ENV, например: make -f mcu.mk pio-quick PIO_ENV=esp32"; \
		exit 1; \
	fi
	@$(PRETTY_RAW) header "Сборка прошивки (env: $(PIO_ENV))"
	@cd $(HARDWARE_MC_DIR) && pio run -e $(PIO_ENV)
	@$(PRETTY_RAW) success "Прошивка собрана!"

# Быстрая загрузка без интерактивного меню
pio-quick-upload: pio-install
	@if [ -z "$(PIO_ENV)" ]; then \
		$(PRETTY_RAW) error "Укажите PIO_ENV, например: make -f mcu.mk pio-quick-upload PIO_ENV=esp32"; \
		exit 1; \
	fi
	@$(PRETTY_RAW) header "Загрузка прошивки (env: $(PIO_ENV))"
	@cd $(HARDWARE_MC_DIR) && pio run -e $(PIO_ENV) -t upload
	@$(PRETTY_RAW) success "Прошивка загружена!"

# Показать список доступных плат через интерактивное меню
pio-boards: deps
	@$(PIO_SELECT) select

# Очистить сборку PlatformIO
pio-clean:
	@$(PRETTY_RAW) header "Очистка PlatformIO"
	@rm -rf $(HARDWARE_MC_DIR)/.pio
	@$(PRETTY_RAW) success "Очищено"

clean: pio-clean
	@rm -f $(MCU_PORT_FILE)
