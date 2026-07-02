![Header](header.png)

<div align="center">

# MemoryLab

**Автоматизированная исследовательская лаборатория иерархии памяти**

[![License](https://img.shields.io/badge/license-GPL--2.0-2C2C2C?style=for-the-badge&labelColor=1E1E1E)](LICENSE.md)
[![C++](https://img.shields.io/badge/c++-17-2C2C2C?style=for-the-badge&logo=cplusplus&labelColor=1E1E1E)]()
[![Python](https://img.shields.io/badge/python-jupyter-2C2C2C?style=for-the-badge&logo=jupyter&labelColor=1E1E1E)]()
[![Typst](https://img.shields.io/badge/typst-0.13-2C2C2C?style=for-the-badge&logo=typst&labelColor=1E1E1E)]()

</div>

Автоматизированная система для лабораторной работы по иерархии памяти ЭВМ МГТУ ИУ-6. Написанный вручную C++ WebSocket-сервер (`HardwareTester`) запускает семь экспериментов с кешем/памятью на CPU хоста, Python-клиент под управлением Jupyter notebook собирает и строит графики результатов, а Typst-пайплайн генерирует и перекомпилирует PDF-отчёт в реальном времени. В качестве нагрузки для эксперимента с самомодифицирующимся кодом используется встроенная сборка Chocolate DOOM как JIT-vs-branching кеш-нагрузка.

## ■ Возможности

- ❖ **Стратификация памяти** — экспериментальное определение размеров кешей L1/L2/L3
- ❖ **Список vs массив** — сравнение времени доступа для связных списков и массивов
- ❖ **Анализ префетча** — измерение эффекта программного предвыборки данных
- ❖ **Оптимизация чтения** — масштабирование многопоточного последовательного чтения памяти
- ❖ **Конфликты кеша** — исследование влияния ассоциативности и геометрии банков/линий
- ❖ **Бенчмарки сортировки** — сравнение производительности алгоритмов с учётом кеш-эффектов
- ❖ **Самомодифицирующийся код** — JIT vs. branching, замер на фоне живого запуска DOOM
- ❖ **Авто-отчёт** — генерация PDF через Typst с `watchdog`-слежением за файлами и автоперекомпиляцией
- ❖ **Настройка одной командой** — `make all` управляет зависимостями, сборкой, отчётом, DOOM и Jupyter Lab

## ■ Стек

<div align="center">

| Компонент | Технология |
|-----------|------------|
| Сервер | C++17, raw-socket WebSocket, OpenSSL |
| Эксперименты | C++17, std::thread |
| Клиент | Python, websockets, rich, matplotlib, numpy |
| Анализ | Jupyter Lab |
| Отчёты | Typst 0.13, typst-bmstu + typst-g7.32-2017 |
| Нагрузка | Chocolate DOOM (CMake) |
| Сборка | Make, vcpkg |

</div>

## ■ Запуск

```bash
git clone https://github.com/pluttan/memorylab.git
cd memorylab
make all     # полная настройка: зависимости, сборка, отчёт, DOOM, Jupyter Lab
make run     # запустить сервер + DOOM + Jupyter Lab
make stop    # остановить все процессы
```

## ■ License

GPL-2.0 © [pluttan](https://github.com/pluttan)
