"""
Прогон всех экспериментов без Jupyter и упаковка результатов в архив.

Параметры берутся из ячеек lab/memory_experiment.ipynb, поэтому числа получаются
те же, что у студента. Запуск: make collect (сервер поднимает Makefile).
Результат: results-<имя компьютера>-<дата>.tgz в корне проекта.
"""

import ast
import asyncio
import json
import re
import socket
import sys
import tarfile
import time
from datetime import datetime
from pathlib import Path

import matplotlib

matplotlib.use("Agg")    # no window: the plots go straight to files

from .hardware_client import HardwareTesterClient, console  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
NOTEBOOK = ROOT / "lab" / "memory_experiment.ipynb"
PLOTS = {
    "cpu_info": None,
    "memory_stratification": "plot_memory_stratification",
    "list_vs_array": "plot_list_vs_array",
    "prefetch": "plot_prefetch",
    "memory_read_optimization": "plot_memory_read_optimization",
    "cache_conflicts": "plot_cache_conflicts",
    "cache_performance": "plot_cache_performance",
    "sorting_algorithms": "plot_sorting_algorithms",
    "self_modifying_code": "plot_self_modifying_code",
}
CALL = re.compile(r'client\.execute\(\s*"(\w+)"\s*(?:,\s*(\{.*?\})\s*)?\)', re.S)


def notebook_calls(path=NOTEBOOK):
    """(функция, параметры) из ячеек тетради в порядке экспериментов."""
    cells = json.loads(path.read_text(encoding="utf-8"))["cells"]
    calls = []
    for cell in cells:
        if cell["cell_type"] != "code":
            continue
        src = "".join(cell["source"])
        for name, params in CALL.findall(src):
            # Comments sit inside the dict literal of every cell
            literal = "\n".join(line.split("#", 1)[0] for line in params.splitlines())
            calls.append((name, ast.literal_eval(literal) if literal.strip() else {}))
    return calls


async def run(out: Path, skip: set):
    client = HardwareTesterClient(host="127.0.0.1", img_dir=str(out / "img"),
                                  results_file=str(out / "results.txt"))
    await client.connect(auto_discover=False)
    summary = {"host": socket.gethostname(), "started": datetime.now().isoformat(timespec="seconds"),
               "experiments": []}
    try:
        for name, params in notebook_calls():
            if name in skip:
                console.print(f"[yellow][[!]][/yellow] Пропуск {name}")
                continue
            start = time.monotonic()
            result = await client.execute(name, params)
            seconds = round(time.monotonic() - start, 1)
            (out / f"{name}.json").write_text(json.dumps(result, ensure_ascii=False, indent=1), encoding="utf-8")
            if name == "cpu_info":
                client.show_cpu_info(result, show_raw=True)
                summary["cpu"] = result.get("cpu", {})
            elif PLOTS.get(name):
                getattr(client, PLOTS[name])(result, show=False)
            summary["experiments"].append({"name": name, "params": params, "seconds": seconds,
                                           "error": result.get("error")})
            console.print(f"[green][[+]][/green] {name}: {seconds} с")
    finally:
        await client.disconnect()
        summary["finished"] = datetime.now().isoformat(timespec="seconds")
        (out / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=1), encoding="utf-8")


def main():
    stamp = datetime.now().strftime("%Y%m%d-%H%M")
    name = f"results-{socket.gethostname()}-{stamp}"
    out = ROOT / "results" / name
    (out / "img").mkdir(parents=True, exist_ok=True)
    # DOOM writes jit_benchmark.csv only while it runs; without the file experiment 8 has nothing to read
    skip = set() if (ROOT / "jit_benchmark.csv").exists() else {"self_modifying_code"}
    asyncio.run(run(out, skip))
    archive = ROOT / f"{name}.tgz"
    with tarfile.open(archive, "w:gz") as tar:
        tar.add(out, arcname=name)
    console.print(f"[green][[+]][/green] Архив: {archive}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
