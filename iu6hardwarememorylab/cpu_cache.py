"""
Идентификация процессора и эксперимент 6 «Производительность кэш-памяти».

Методы подмешиваются в HardwareTesterClient: используют его _save_results,
_get_save_path и results_file.
"""

import json
from pathlib import Path
from typing import Any, Dict, List, Optional

import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter
from rich import box
from rich.console import Console
from rich.panel import Panel
from rich.table import Table

console = Console()

# Имя файла со сведениями о процессоре рядом с results.txt; его читает генератор отчёта
CPU_INFO_FILE = "cpu_info.json"

TYPE_NAMES = {
    "Data": "данные",
    "Instruction": "команды",
    "Unified": "общий",
    "Load": "чтение",
    "Store": "запись",
}


def format_bytes(value: int) -> str:
    """Размер в байтах в читаемом виде: Б, КБ, МБ, ГБ."""
    if not value:
        return "—"
    for unit, size in (("ГБ", 1 << 30), ("МБ", 1 << 20), ("КБ", 1 << 10)):
        if value >= size:
            number = value / size
            return f"{number:.0f} {unit}" if number == int(number) else f"{number:.2f} {unit}"
    return f"{value} Б"


def cache_rows(caches: List[Dict[str, Any]]) -> List[List[str]]:
    """Строки таблицы кэшей: уровень, тип, объём, линейка, ассоциативность, наборы, банк."""
    rows = []
    for c in caches:
        ways = "полная" if c.get("fully_associative") else (str(c["ways"]) if c.get("ways") else "—")
        rows.append([
            f"L{c['level']}",
            TYPE_NAMES.get(c["type"], c["type"]),
            format_bytes(c["size_bytes"]),
            f"{c['line_bytes']} Б" if c.get("line_bytes") else "—",
            ways,
            str(c["sets"]) if c.get("sets") else "—",
            format_bytes(c.get("bank_bytes", 0)),
        ])
    return rows


def tlb_rows(tlbs: List[Dict[str, Any]]) -> List[List[str]]:
    """Строки таблицы TLB: уровень, тип, страницы, записей, ассоциативность."""
    rows = []
    for t in tlbs:
        ways = "полная" if t.get("fully_associative") else (str(t["ways"]) if t.get("ways") else "—")
        rows.append([f"L{t['level']}", TYPE_NAMES.get(t["type"], t["type"]), t["pages"], str(t["entries"]), ways])
    return rows


CACHE_HEADERS = ["Уровень", "Тип", "Объём", "Линейка", "Ассоциативность", "Наборов", "Банк"]
TLB_HEADERS = ["Уровень", "Тип", "Страницы", "Записей", "Ассоциативность"]
TLB_MISSING = ("Процессор не сообщает параметры TLB. "
               "Возьмите их из документации процессора (раздел TLB или Translation Lookaside Buffer).")


class CpuCacheMixin:
    """Идентификация процессора и график производительности кэш-памяти."""

    # ==========================
    # ===  CPU identification ===
    # ==========================

    def show_cpu_info(self, data: Dict[str, Any], show_raw: bool = True) -> None:
        """Выводит идентификацию процессора и сохраняет её для отчёта."""
        if "error" in data:
            console.print(f"[red][[-]][/red] Ошибка в данных: {data['error']}")
            return

        cpu = data.get("cpu", {})
        memory = data.get("memory", {})
        tbl = Table(show_header=False, box=box.SIMPLE, padding=(0, 1))
        tbl.add_column("Key", style="bold")
        tbl.add_column("Value")
        tbl.add_row("Производитель", cpu.get("vendor") or "—")
        tbl.add_row("Модель", cpu.get("brand") or "—")
        tbl.add_row("Архитектура", cpu.get("arch", "—"))
        if cpu.get("family"):
            tbl.add_row("Семейство / модель / степпинг",
                        f"{cpu['family']} / {cpu['model']} / {cpu['stepping']}")
        tbl.add_row("Логических процессоров", str(cpu.get("logical_cpus", "—")))
        tbl.add_row("Объём физической памяти", format_bytes(memory.get("physical_bytes", 0)))
        tbl.add_row("Размер страницы памяти", format_bytes(memory.get("page_size_bytes", 0)))
        tbl.add_row("Размер линейки кэша", f"{memory.get('cache_line_bytes', '—')} Б")
        console.print(Panel(tbl, title="ИДЕНТИФИКАЦИЯ ПРОЦЕССОРА", border_style="green"))

        self._print_rows("Кэш-память", CACHE_HEADERS, cache_rows(data.get("caches", [])))
        tlbs = data.get("tlb", [])
        if tlbs:
            self._print_rows("Буферы TLB", TLB_HEADERS, tlb_rows(tlbs))
        else:
            console.print(f"[yellow][[!]][/yellow] {TLB_MISSING}")

        raw = data.get("raw_cpuid", [])
        if show_raw and raw:
            self._print_rows("CPUID (лист: EAX EBX ECX EDX)", ["Лист", "EAX", "EBX", "ECX", "EDX"],
                             [[r["leaf"], r["eax"], r["ebx"], r["ecx"], r["edx"]] for r in raw])

        if self.results_file:
            path = Path(self.results_file).with_name(CPU_INFO_FILE)
            path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
            console.print(f"[green][[+]][/green] Сведения сохранены для отчёта: {path}")

    @staticmethod
    def _print_rows(title: str, headers: List[str], rows: List[List[str]]) -> None:
        tbl = Table(box=box.SIMPLE, padding=(0, 1))
        for header in headers:
            tbl.add_column(header)
        for row in rows:
            tbl.add_row(*row)
        console.print(Panel(tbl, title=title, border_style="cyan"))

    # ==================================
    # ===  Experiment 6: cache speed ===
    # ==================================

    def plot_cache_performance(self, data: Dict[str, Any],
                               save_path: Optional[str] = None,
                               show: bool = True) -> Optional[plt.Figure]:
        """Строит график производительности кэш-памяти (эксперимент 6)."""
        if "error" in data:
            console.print(f"[red][[-]][/red] Ошибка в данных: {data['error']}")
            return None
        points = data.get("dataPoints", [])
        if not points:
            console.print("[red][[-]][/red] Нет данных для построения графика")
            return None

        conclusions = data.get("conclusions", {})
        tbl = Table(show_header=False, box=box.SIMPLE, padding=(0, 1))
        tbl.add_column("Key", style="bold")
        tbl.add_column("Value")
        for n in (1, 2, 3):
            step = conclusions.get(f"step{n}_bytes", 0)
            os_size = conclusions.get(f"os_l{n}_bytes", 0)
            tbl.add_row(f"Ступень {n} (по графику)", format_bytes(step))
            tbl.add_row(f"L{n} по данным ОС", format_bytes(os_size))
        tbl.add_row("Чтение 1 КБ: от / до",
                    f"{conclusions.get('min_read_ns_per_kb', 0):.2f} / "
                    f"{conclusions.get('max_read_ns_per_kb', 0):.2f} нс")
        console.print(Panel(tbl, title="ВЫВОДЫ ЭКСПЕРИМЕНТА 6: Производительность кэш-памяти",
                            border_style="green"))

        self._save_results("Эксперимент 6: Производительность кэш-памяти",
                           data.get("parameters", {}), conclusions)

        sizes_kb = [p["block_bytes"] / 1024 for p in points]
        series = [
            ("read_ns_per_kb", "r-", "Чтение"),
            ("write_ns_per_kb", "g-", "Запись"),
            ("read_write_ns_per_kb", "m-", "Чтение и запись"),
            ("write_read_ns_per_kb", "b-", "Запись, затем чтение"),
        ]
        fig, ax = plt.subplots(figsize=(12, 7), facecolor="white")
        for key, style, label in series:
            ax.plot(sizes_kb, [p[key] for p in points], style, linewidth=1.5, label=label)
        ax.set_xscale("log", base=2)
        ax.xaxis.set_major_formatter(FuncFormatter(lambda kb, _: format_bytes(int(kb * 1024))))
        ax.set_xlabel("Размер обрабатываемого блока", fontsize=12)
        ax.set_ylabel("Время обработки 1 КБ блока (нс)", fontsize=12)
        ax.set_title("Исследование производительности кэш-памяти", fontsize=14, fontweight="bold")
        ax.grid(True, which="both", linestyle="--", alpha=0.5)

        # Объёмы кэша по данным ОС: ступени графика должны лежать рядом с этими линиями
        for n in (1, 2, 3):
            size = conclusions.get(f"os_l{n}_bytes", 0)
            if size and size / 1024 <= sizes_kb[-1]:
                ax.axvline(size / 1024, color="gray", linestyle=":", linewidth=1.2)
                ax.text(size / 1024, 0.97, f" L{n} = {format_bytes(size)}", color="gray",
                        transform=ax.get_xaxis_transform(), va="top", fontsize=9)
        ax.legend(loc="upper left", bbox_to_anchor=(0.0, 0.88))

        params = data.get("parameters", {})
        info_text = (
            f"Параметры эксперимента:\n"
            f"  • Наибольший блок: {params.get('param1_kb', '?')} КБ\n"
            f"  • Шаг приращения: {params.get('param2_b', '?')} Б\n"
            f"  • Линейка кэша: {params.get('cache_line_bytes', '?')} Б"
        )
        ax.text(0.02, 0.60, info_text, transform=ax.transAxes, fontsize=9, va="top",
                bbox=dict(boxstyle="round", facecolor="wheat", alpha=0.8))
        plt.tight_layout()

        actual_save_path = self._get_save_path("exp6_cache_performance", save_path)
        if actual_save_path:
            fig.savefig(actual_save_path, dpi=150, bbox_inches="tight")
            console.print(f"[green][[+]][/green] График сохранён: {actual_save_path}")
        if show:
            plt.show()
            plt.close(fig)
            return None
        return fig
