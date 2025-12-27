# Лабораторная работа №4: Исследование памяти ЭВМ

Автоматизированная система для выполнения лабораторной работы по исследованию иерархии памяти компьютера.

## Быстрый старт

```bash
# Для компьютеров на кафедре раскоментируйте:
# sudo apt update
# sudo apt install -y python3-dev python3-venv ttf-mscorefonts-installer libsdl2-dev libsdl2-mixer-dev libsdl2-net-dev
# Клонируем в папку с вашей фамилией (транслит)
folder=lab4_Фамилия
git clone https://github.com/pluttan/memorylab.git $folder
cd $folder
make all
```

Эта команда:
1. Скачает и настроит Typst для генерации отчёта
2. Скомпилирует сервер HardwareTester
3. Создаст виртуальное окружение Python и установит зависимости
4. Запросит данные для титульной страницы
5. Сгенерирует шаблон отчёта и скомпилирует PDF
6. Запустит режим наблюдения за отчётом (автокомпиляция)
7. Запустит сервер и Jupyter Lab

## Структура проекта

```
├── hardware/              # C++ сервер HardwareTester (для ПК)
│   ├── server.cpp         # WebSocket сервер
│   ├── functions.cpp      # Эксперименты с памятью
│   └── tester.cpp         # Класс для измерений
├── hardware-mc/           # Версия для микроконтроллеров
│   ├── platformio.ini     # Конфигурация PlatformIO
│   ├── main.cpp           # UART интерфейс
│   └── experiments/       # Эксперименты для МК
├── iu6hardwarememorylab/  # Python-клиент
│   ├── hardware_client.py # Клиент для сервера
│   ├── generatereport.py  # Генератор отчёта
│   ├── watch_report.py    # Автокомпиляция отчёта
│   └── pretty_print.py    # Красивый вывод
├── lab/                   # Jupyter notebook
│   └── memory_experiment.ipynb
├── report/                # Отчёт (генерируется)
└── Makefile               # Автоматизация
```

## Основные команды

| Команда | Описание |
|---------|----------|
| `make all` | Полная настройка и запуск |
| `make run` | Запуск сервера + Jupyter Lab |
| `make clean` | Очистка всех генерируемых файлов |
| `make report-setup` | Настройка окружения для отчёта |
| `make report-watch` | Запуск автокомпиляции отчёта |
| `make logs` | Показать логи |
| `make help` | Справка по командам |

## PlatformIO (микроконтроллеры)

Для запуска экспериментов на микроконтроллерах:

```bash
# Установить PlatformIO
make pio-install

# Собрать и загрузить (по умолчанию ESP32)
make pio-build
make pio-upload

# UART монитор
make pio-monitor

# Другая плата (например STM32 Black Pill)
make pio-build PIO_ENV=stm32f411

# Список всех плат
make pio-boards
```

Подробнее: [hardware-mc/README.md](hardware-mc/README.md)

## Эксперименты

1. **Стратификация памяти** — определение размеров кэшей L1, L2, L3
2. **Список vs Массив** — сравнение времени доступа
3. **Prefetch** — влияние предвыборки данных
4. **Оптимизация чтения** — последовательный vs случайный доступ
5. **Конфликты кэша** — влияние ассоциативности
6. **Алгоритмы сортировки** — сравнение производительности

## Требования

- macOS / Linux
- Python 3.9+, python-venv, python-dev
- g++ с поддержкой C++17
- OpenSSL (для WebSocket)
- Git (для клонирования пакетов Typst)
- Times New Roman
- libsdl2

## Лицензия

GNU General Public License v2 (GPL-2.0)
[LICENSE.md](LICENSE.md).
