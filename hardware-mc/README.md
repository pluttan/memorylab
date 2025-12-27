# Hardware Memory Lab for Microcontrollers

Порт лабораторной работы по исследованию памяти для микроконтроллеров.
Использует PlatformIO для универсальной сборки под разные платформы.

## Быстрый старт

```bash
# Из корня проекта:

# 1. Установить PlatformIO (если не установлен)
make pio-install

# 2. Собрать для ESP32 (по умолчанию)
make pio-build

# 3. Загрузить на плату
make pio-upload

# 4. Открыть UART-монитор
make pio-monitor
```

### Выбор платформы

```bash
# STM32 Black Pill
make pio-build PIO_ENV=stm32f411

# Arduino Uno
make pio-build PIO_ENV=uno

# ESP8266 NodeMCU
make pio-build PIO_ENV=esp8266

# Raspberry Pi Pico
make pio-build PIO_ENV=pico

# Список всех плат
make pio-boards
```

## Поддерживаемые платформы

| Окружение | Плата | RAM | Кэш |
|-----------|-------|-----|-----|
| `esp32` | ESP32 DevKit | 320 KB | Да |
| `esp32s3` | ESP32-S3 + PSRAM | 512 KB+ | Да |
| `esp8266` | NodeMCU | 80 KB | Нет |
| `stm32f103` | Blue Pill | 20 KB | Нет |
| `stm32f401` | Black Pill F401 | 96 KB | Нет |
| `stm32f411` | Black Pill F411 | 128 KB | Нет |
| `nucleo_f446re` | Nucleo F446RE | 128 KB | Нет |
| `nucleo_f746zg` | Nucleo F746ZG | 320 KB | **Да (L1)** |
| `uno` | Arduino Uno | 2 KB | Нет |
| `mega` | Arduino Mega | 8 KB | Нет |
| `nano` | Arduino Nano | 2 KB | Нет |
| `pico` | Raspberry Pi Pico | 264 KB | Нет |

## Эксперименты

1. **Расслоение памяти** — определение границ кэшей (на МК с кэшем)
2. **Список vs Массив** — влияние локальности данных
3. **Предвыборка** — последовательный vs случайный доступ
4. **Оптимизация чтения** — побайтовое vs пословное чтение
5. **Конфликты кэш-памяти** — влияние ассоциативности
6. **Алгоритмы сортировки** — сравнение O(n²) и O(n log n)

## Использование

После загрузки прошивки откройте UART-монитор (115200 baud) и отправляйте команды:

| Команда | Описание |
|---------|----------|
| `1` | Memory Stratification |
| `2` | List vs Array |
| `3` | Prefetch / Access Pattern |
| `4` | Memory Read Optimization |
| `5` | Cache Conflicts |
| `6` | Sorting Algorithms |
| `a` | Run All Experiments |
| `h` | Show Help |

## Структура проекта

```
hardware-mc/
├── platformio.ini          # Конфигурация PlatformIO
├── main.cpp                # Главный файл с UART меню
├── experiments/
│   ├── common.hpp          # Платформо-зависимые утилиты
│   ├── memory_stratification.cpp
│   ├── list_vs_array.cpp
│   ├── prefetch.cpp
│   ├── memory_read_optimization.cpp
│   ├── cache_conflicts.cpp
│   └── sorting_algorithms.cpp
└── README.md
```

## Вывод результатов

Результаты выводятся через UART в JSON формате:

```json
{"experiment":"list_vs_array","elements":500,"array_time_us":123.45,"list_time_us":567.89,"list_to_array_ratio":4.602}
```

## Настройка под ваш МК

Параметры в `experiments/common.hpp`:

```cpp
#define AVAILABLE_RAM 10000     // Доступная RAM в байтах
#define CPU_FREQ_MHZ 72         // Частота CPU в МГц
#define CACHE_LINE_SIZE 32      // Линейка кэша (для ARM Cortex-M7)
#define CACHE_BANK_SIZE 8192    // Банк кэша L1
```

## Особенности платформ

- **AVR (Arduino Uno/Nano/Mega)**: Нет кэша, ограниченная RAM. Эффекты предвыборки минимальны.
- **STM32F1/F4**: Нет D-кэша, есть Flash ART Accelerator. Эксперименты показывают влияние Flash задержек.
- **STM32F7/H7**: Есть полноценный L1 кэш, все эксперименты актуальны.
- **ESP32**: Есть кэш для внешней Flash/PSRAM, интересные результаты.
- **RP2040 (Pico)**: Есть XIP кэш для Flash, нет D-кэша.

## Интеграция с Jupyter Notebook

После загрузки прошивки на МК можно использовать UART-WebSocket сервер для работы через Jupyter:

```bash
# 1. Загрузить прошивку
make pio

# 2. Запустить UART-сервер + Jupyter Lab
make mcu-run

# Или только UART-сервер (интерактивный выбор порта)
make server-mcu
```

В Jupyter используется тот же клиент что и для desktop-версии:

```python
from iu6hardwarememorylab import HardwareTesterClient

client = HardwareTesterClient()
await client.connect()

# Список экспериментов
functions = await client.list_functions()

# Выполнение эксперимента
result = await client.call("memory_stratification")
```

### Архитектура

```
┌──────────────┐     WebSocket      ┌──────────────┐      UART       ┌─────────┐
│ Jupyter      │ ←────────────────→ │ uart_server  │ ←─────────────→ │   МК    │
│ (клиент)     │     localhost:8765 │ (Python)     │  /dev/ttyUSB0   │ (ESP32) │
└──────────────┘                    └──────────────┘                 └─────────┘
```

## Лицензия

GNU General Public License v2 (GPL-2.0)
