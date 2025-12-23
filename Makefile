# Makefile для лабораторной работы 4
# Поднимает сервер HardwareTester и Jupyter Notebook

# Директории
HARDWARE_DIR = hardware
TEST_DIR = lab
VENV_DIR = .venv
LOG_DIR = log
IMG_DIR = report/img
REPORT_DIR = report

# Typst
TYPST_VERSION = 0.13.0
TYPST_BMSTU_REPO = https://github.com/bmstudents/typst-bmstu.git
TYPST_G732_REPO = https://github.com/bmstudents/typst-g7.32-2017.git
TYPST_BMSTU_DIR = $(REPORT_DIR)/typst-bmstu
TYPST_G732_DIR = $(REPORT_DIR)/typst-g7.32-2017
TYPST_BIN_DIR = $(REPORT_DIR)/typst
TYPST_BIN = $(TYPST_BIN_DIR)/typst

# Определение ОС и архитектуры для Typst
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S),Darwin)
    ifeq ($(UNAME_M),arm64)
        TYPST_ARCHIVE = typst-aarch64-apple-darwin.tar.xz
    else
        TYPST_ARCHIVE = typst-x86_64-apple-darwin.tar.xz
    endif
else ifeq ($(UNAME_S),Linux)
    ifeq ($(UNAME_M),x86_64)
        TYPST_ARCHIVE = typst-x86_64-unknown-linux-musl.tar.xz
    else ifeq ($(UNAME_M),aarch64)
        TYPST_ARCHIVE = typst-aarch64-unknown-linux-musl.tar.xz
    else
        TYPST_ARCHIVE = typst-x86_64-unknown-linux-musl.tar.xz
    endif
else
    TYPST_ARCHIVE = typst-x86_64-unknown-linux-musl.tar.xz
endif

TYPST_URL = https://github.com/typst/typst/releases/download/v$(TYPST_VERSION)/$(TYPST_ARCHIVE)
REPORT_MAIN = $(REPORT_DIR)/main.typ
REPORT_PDF = $(REPORT_DIR)/main.pdf

# Файлы
SERVER_SRC = $(HARDWARE_DIR)/server.cpp
SERVER_BIN = $(HARDWARE_DIR)/server
NOTEBOOK = memory_experiment.ipynb

# Логи
SERVER_LOG = $(LOG_DIR)/server.log
JUPYTER_LOG = $(LOG_DIR)/jupyter.log

# Флаги компиляции
CXX = g++
CXXFLAGS = -std=c++17 -O2 -pthread

# OpenSSL пути для macOS (Homebrew)
OPENSSL_PREFIX := $(shell brew --prefix openssl 2>/dev/null || echo "/usr/local/opt/openssl")
CXXFLAGS += -I$(OPENSSL_PREFIX)/include
LDFLAGS = -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto

# Порты
SERVER_PORT = 8765
JUPYTER_PORT = 8888

# Python в виртуальном окружении
PYTHON = $(VENV_DIR)/bin/python
PIP = $(VENV_DIR)/bin/pip

# Форматирование вывода через rich (если доступен venv)
PRETTY = $(PYTHON) -m iu6hardwarememorylab.pretty_print
PRETTY_RAW = python3 iu6hardwarememorylab/pretty_print.py

# Форматирование вывода (fallback)
LINE = ========================================

.PHONY: all server jupyter notebook build clean help stop venv deps kill-server kill-port logs report report-setup report-watch report-generate report-compile typst-install clean-report run

# По умолчанию показываем помощь
help:
	@echo "$(LINE)"
	@echo " Memory Lab - Makefile"
	@echo "$(LINE)"
	@echo " make build    - Собрать сервер"
	@echo " make server   - Запустить сервер HardwareTester"
	@echo " make notebook - Запустить Jupyter Notebook"
	@echo " make lab      - Запустить Jupyter Lab"
	@echo " make run      - Запустить сервер и lab"
	@echo " make venv     - Создать виртуальное окружение"
	@echo " make deps     - Установить зависимости Python"
	@echo " make stop     - Остановить все процессы"
	@echo " make logs     - Показать логи"
	@echo " make clean    - Удалить всё"
	@echo ""
	@echo " Отчёт:"
	@echo " make all             - Сгенерировать/обновить отчёт (PDF в корне)"
	@echo " make report-setup    - Настроить окружение для отчёта"
	@echo " make report-generate - Сгенерировать шаблон main.typ"
	@echo " make report-watch    - Режим наблюдения (автокомпиляция)"
	@echo "$(LINE)"

# Создать папки для логов и изображений
$(LOG_DIR):
	@mkdir -p $(LOG_DIR)

$(IMG_DIR):
	@mkdir -p $(IMG_DIR)

# Создать виртуальное окружение
$(VENV_DIR)/bin/activate:
	@$(PRETTY_RAW) header "Создание виртуального окружения"
	python3 -m venv $(VENV_DIR)
	@$(PRETTY_RAW) success "Путь: $(VENV_DIR)"

venv: $(VENV_DIR)/bin/activate

# Установить зависимости
$(VENV_DIR)/.deps_installed: $(VENV_DIR)/bin/activate
	@$(PRETTY_RAW) header "Установка зависимостей Python"
	@$(PIP) install --upgrade pip --progress-bar off
	@$(PIP) install jupyter notebook websockets netifaces matplotlib numpy watchdog rich --progress-bar off
	@touch $(VENV_DIR)/.deps_installed
	@$(PRETTY_RAW) success "Зависимости установлены"

deps: $(VENV_DIR)/.deps_installed

# Собрать сервер
build: $(SERVER_BIN)

$(SERVER_BIN): $(SERVER_SRC) $(HARDWARE_DIR)/functions.cpp $(HARDWARE_DIR)/tester.cpp
	@$(PRETTY_RAW) header "Компиляция сервера"
	@$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS)
	@$(PRETTY_RAW) success "Сервер скомпилирован: $(SERVER_BIN)"

# Убить процесс на порту (кросс-платформенно: macOS + Linux)
kill-port:
	@PID=$$(lsof -ti:$(SERVER_PORT) 2>/dev/null || ss -tlnp 2>/dev/null | grep :$(SERVER_PORT) | sed -n 's/.*pid=\([0-9]*\).*/\1/p' | head -1); \
	if [ -n "$$PID" ]; then \
		$(PRETTY_RAW) header "Остановка процесса на порту $(SERVER_PORT)"; \
		$(PRETTY_RAW) info "PID: $$PID"; \
		kill -9 $$PID 2>/dev/null || true; \
		sleep 1; \
		$(PRETTY_RAW) success "Процесс остановлен"; \
	fi; \
	true

# Убить сервер если запущен
kill-server: kill-port
	-@pkill -f "$(SERVER_BIN)" 2>/dev/null || true

# Запустить сервер (с логированием в консоль и файл)
server: build kill-server $(LOG_DIR)
	@cd $(HARDWARE_DIR) && ./server 2>&1 | tee ../$(SERVER_LOG)

# Запустить Jupyter Notebook
notebook: deps $(LOG_DIR)
	@$(PRETTY) header "Jupyter Notebook"
	@$(PRETTY) info "Port: $(JUPYTER_PORT)"
	@$(PRETTY) success "URL: http://localhost:$(JUPYTER_PORT)"
	@$(PRETTY) info "Log: $(JUPYTER_LOG)"
	@cd $(TEST_DIR) && ./../$(PYTHON) -m jupyter notebook --port=$(JUPYTER_PORT) --no-browser $(NOTEBOOK) > ../$(JUPYTER_LOG) 2>&1

# Запустить Jupyter Lab
lab: deps $(LOG_DIR)
	@$(PRETTY) header "Jupyter Lab"
	@$(PRETTY) info "Port: $(JUPYTER_PORT)"
	@$(PRETTY) success "URL: http://localhost:$(JUPYTER_PORT)"
	@$(PRETTY) info "Log: $(JUPYTER_LOG)"
	@cd $(TEST_DIR) && ./../$(PYTHON) -m jupyter lab --port=$(JUPYTER_PORT) --no-browser > ../$(JUPYTER_LOG) 2>&1

# Запустить всё (сервер в фоне + lab)
run: build deps kill-server $(LOG_DIR) $(IMG_DIR)
	@cd $(HARDWARE_DIR) && ./server 2>&1 | tee ../$(SERVER_LOG) &
	@sleep 2
	@$(PRETTY) header "Jupyter Lab"
	@$(PRETTY) info "Port: $(JUPYTER_PORT)"
	@$(PRETTY) success "URL: http://localhost:$(JUPYTER_PORT)"
	@$(PRETTY) info "Log: $(JUPYTER_LOG)"
	@cd $(TEST_DIR) && ./../$(PYTHON) -m jupyter lab --port=$(JUPYTER_PORT) > ../$(JUPYTER_LOG) 2>&1

# Главная цель: установка всего, настройка отчёта, запуск сервера + lab + режима наблюдения
all: report-setup build deps kill-server $(LOG_DIR) $(IMG_DIR)
	@$(PRETTY) header "Настройка и запуск всех компонентов"
	@# Интерактивная настройка отчёта (генерация main.typ)
	@$(PYTHON) -c "from iu6hardwarememorylab import generate_report; generate_report(interactive=True, perform_build=False)"
	@$(PRETTY) info "Компиляция PDF..."
	@$(TYPST_BIN) compile $(REPORT_MAIN) $(REPORT_PDF)
	@# Копируем PDF в корень с именем из конфига
	@$(PYTHON) -c "import shutil, json; from pathlib import Path; c=json.load(open('report/.report_config.json')); name=f'Отчет_{c[\"author_name\"]}_{c[\"author_group\"]}.pdf'; shutil.copy2('report/main.pdf', name)"
	@$(PRETTY) success "PDF готов!"
	@$(PRETTY) info "Запуск режима наблюдения за отчётом (фон)..."
	@$(PYTHON) -m iu6hardwarememorylab.watch_report > $(LOG_DIR)/report_watch.log 2>&1 &
	@$(PRETTY) info "Запуск сервера HardwareTester (фон)..."
	@cd $(HARDWARE_DIR) && ./server > ../$(SERVER_LOG) 2>&1 &
	@sleep 2
	@$(PRETTY) info "Запуск Jupyter Lab..."
	@$(PRETTY) success "URL: http://localhost:$(JUPYTER_PORT)"
	@$(PRETTY) info "Логи: $(LOG_DIR)/"
	@cd $(TEST_DIR) && ./../$(PYTHON) -m jupyter lab --port=$(JUPYTER_PORT) > ../$(JUPYTER_LOG) 2>&1

# Показать логи
logs:
	@$(PRETTY_RAW) header "Лог сервера: $(SERVER_LOG)"
	@cat $(SERVER_LOG) 2>/dev/null || echo "(пусто)"
	@echo ""
	@$(PRETTY_RAW) header "Лог Jupyter: $(JUPYTER_LOG)"
	@cat $(JUPYTER_LOG) 2>/dev/null || echo "(пусто)"

# Остановить все процессы
stop: kill-server
	@$(PRETTY_RAW) header "Остановка процессов"
	-@pkill -f "jupyter-notebook" 2>/dev/null || true
	-@pkill -f "jupyter-lab" 2>/dev/null || true
	-@pkill -f "jupyter notebook" 2>/dev/null || true
	@$(PRETTY_RAW) success "Процессы остановлены"

# Очистить скомпилированные файлы
clean: stop
	@$(PRETTY_RAW) header "Очистка"
	@rm -f $(SERVER_BIN)
	@rm -rf $(VENV_DIR)
	@rm -rf $(LOG_DIR)
	@rm -rf $(REPORT_DIR)
	@rm -f Отчет*.pdf
	@rm -rf __pycache__ $(TEST_DIR)/__pycache__
	@$(PRETTY_RAW) success "Очищено"

# ==================== ОТЧЁТ ====================

# Скачать и распаковать Typst 0.12.0 локально
$(TYPST_BIN):
	@$(PRETTY_RAW) header "Скачивание Typst $(TYPST_VERSION)"
	@mkdir -p $(TYPST_BIN_DIR)
	@$(PRETTY_RAW) info "URL: $(TYPST_URL)"
	@curl -L -o $(TYPST_BIN_DIR)/$(TYPST_ARCHIVE) $(TYPST_URL)
	@$(PRETTY_RAW) info "Распаковка..."
	@cd $(TYPST_BIN_DIR) && tar -xf $(TYPST_ARCHIVE) --strip-components=1
	@rm -f $(TYPST_BIN_DIR)/$(TYPST_ARCHIVE)
	@chmod +x $(TYPST_BIN)
	@$(PRETTY_RAW) success "Typst $(TYPST_VERSION) установлен: $(TYPST_BIN)"

typst-install: $(TYPST_BIN)

# Клонировать репозитории для отчёта
$(TYPST_BMSTU_DIR):
	@$(PRETTY_RAW) header "Клонирование typst-bmstu"
	@mkdir -p $(REPORT_DIR)
	@git clone $(TYPST_BMSTU_REPO) $(TYPST_BMSTU_DIR)
	@$(PRETTY_RAW) success "Склонировано: $(TYPST_BMSTU_DIR)"

$(TYPST_G732_DIR):
	@$(PRETTY_RAW) header "Клонирование typst-g7.32-2017"
	@mkdir -p $(REPORT_DIR)
	@git clone $(TYPST_G732_REPO) $(TYPST_G732_DIR)
	@$(PRETTY_RAW) success "Склонировано: $(TYPST_G732_DIR)"

# Настроить окружение для отчёта (скачивание Typst + клонирование)
report-setup: $(TYPST_BIN) $(TYPST_BMSTU_DIR) $(TYPST_G732_DIR)
	@$(PRETTY_RAW) header "Окружение для отчёта готово"
	@$(PRETTY_RAW) info "Typst: $$($(TYPST_BIN) --version 2>/dev/null || echo 'не установлен')"
	@$(PRETTY_RAW) info "typst-bmstu: $(TYPST_BMSTU_DIR)"
	@$(PRETTY_RAW) info "typst-g7.32-2017: $(TYPST_G732_DIR)"

# Главная цель: генерация, переименование (если нужно) и запуск сборки через Python
report: report-setup deps
	@echo "$(LINE)"
	@echo " Генерация отчёта"
	@echo "$(LINE)"
	@$(PYTHON) -c "from iu6hardwarememorylab import generate_report; generate_report(interactive=True, perform_build=True)"

# Финальная стадия: компиляция и запуск наблюдения (вызывается из Python)
report-finish:
	@echo "$(LINE)"
	@echo " Компиляция отчёта"
	@echo "$(LINE)"
	@$(TYPST_BIN) compile $(REPORT_MAIN) $(REPORT_PDF)
	@echo " Status: Готово"
	@echo " PDF: $(REPORT_PDF)"
	@echo "$(LINE)"
	@echo " Запуск режима наблюдения..."
	@$(PYTHON) -m iu6hardwarememorylab.watch_report

# Сгенерировать шаблон отчёта без интерактивных промптов
report-generate: $(TYPST_BMSTU_DIR) $(TYPST_G732_DIR) deps
	@echo "$(LINE)"
	@echo " Генерация шаблона отчёта"
	@echo "$(LINE)"
	@$(PYTHON) -c "from iu6hardwarememorylab import generate_report; generate_report()"
	@echo "$(LINE)"

# Скомпилировать отчёт (только компиляция)
report-compile: $(TYPST_BIN)
	@echo "$(LINE)"
	@echo " Компиляция отчёта"
	@echo "$(LINE)"
	@if [ ! -f "$(REPORT_MAIN)" ]; then \
		echo " [!] Файл $(REPORT_MAIN) не найден"; \
		echo " Выполните: make report"; \
		exit 1; \
	fi
	@$(TYPST_BIN) compile $(REPORT_MAIN) $(REPORT_PDF)
	@echo " Status: Готово"
	@echo " PDF: $(REPORT_PDF)"
	@echo "$(LINE)"

# Режим наблюдения (автокомпиляция при изменениях)
report-watch: $(TYPST_BIN) $(TYPST_BMSTU_DIR) $(TYPST_G732_DIR) deps
	@echo "$(LINE)"
	@echo " Режим наблюдения за отчётом"
	@echo "$(LINE)"
	@if [ ! -f "$(REPORT_MAIN)" ]; then \
		echo " [!] Файл $(REPORT_MAIN) не найден"; \
		echo " Выполните: make report"; \
		exit 1; \
	fi
	@$(PYTHON) -m iu6hardwarememorylab.watch_report

# Очистить отчёт (удалить склонированные репозитории)
clean-report:
	@echo "$(LINE)"
	@echo " Очистка отчёта"
	@echo "$(LINE)"
	@rm -rf $(TYPST_BMSTU_DIR)
	@rm -rf $(TYPST_G732_DIR)
	@rm -rf $(TYPST_BIN_DIR)
	@rm -f $(REPORT_PDF)
	@echo " Status: Готово"
	@echo "$(LINE)"
