<div align="center">

# MemoryLab

**Automated memory hierarchy research lab**

</div>

An automated system for the BMSTU ИУ-6 computer memory hierarchy lab. A hand-rolled C++ WebSocket server (`HardwareTester`) runs seven cache/memory experiments on the host CPU, a Python client driven from a Jupyter notebook collects and plots the results, and a Typst pipeline generates and live-recompiles the PDF report. A bundled Chocolate DOOM build is used as a JIT vs. branching cache-load workload for the self-modifying-code experiment.

## ■ Features

- ❖ **Memory stratification** — detect L1/L2/L3 cache sizes experimentally
- ❖ **List vs array** — compare access time for linked lists and arrays
- ❖ **Prefetch analysis** — measure the effect of software prefetching
- ❖ **Read optimization** — multithreaded sequential memory read scaling
- ❖ **Cache conflicts** — study the impact of set associativity and bank/line geometry
- ❖ **Sorting benchmarks** — compare algorithm performance with cache effects
- ❖ **Self-modifying code** — JIT vs. branching, benchmarked against a live DOOM run
- ❖ **Auto-report** — Typst PDF generation with `watchdog` file-watch and auto-recompilation
- ❖ **One-command setup** — `make all` handles deps, build, report, DOOM and Jupyter Lab

## ■ Stack

<div align="center">

| Component | Technology |
|-----------|------------|
| Server | C++17, raw-socket WebSocket, OpenSSL |
| Experiments | C++17, std::thread |
| Client | Python, websockets, rich, matplotlib, numpy |
| Analysis | Jupyter Lab |
| Reports | Typst 0.13, typst-bmstu + typst-g7.32-2017 |
| Workload | Chocolate DOOM (CMake) |
| Build | Make, vcpkg |

</div>

## ■ How It Works

```
1. `make all` installs dependencies via vcpkg, builds the C++ HardwareTester server and Chocolate DOOM.
2. The HardwareTester WebSocket server starts and executes seven cache/memory experiments on the host CPU.
3. The Python client connects over WebSocket from Jupyter Lab, collects results, and plots them with matplotlib/numpy.
4. Typst watches the output directory via watchdog and live-recompiles the PDF lab report on every change.
```

## ■ Usage

```bash
git clone https://github.com/pluttan/memorylab.git
cd memorylab
make all     # full setup: deps, build, report, DOOM, Jupyter Lab
make run     # start server + DOOM + Jupyter Lab
make stop    # stop all processes
```

## ■ License

GPL-2.0 © [pluttan](https://github.com/pluttan)
