"""
Клиент для сервера на микроконтроллере (UART-WebSocket мост).

Основной ход лабораторной работы его не использует. Прошивка и мост
запускаются отдельно: make -f mcu.mk help
"""

import os
from typing import Any, Dict, Optional

import matplotlib.pyplot as plt
from rich import box
from rich.panel import Panel
from rich.table import Table

try:
    from .hardware_client import HardwareTesterClient, console
except ImportError:
    from hardware_client import HardwareTesterClient, console

# ==================== MCU CLIENT ====================

class MCUClient(HardwareTesterClient):
    """
    Клиент для подключения к микроконтроллеру через UART-WebSocket сервер.
    
    Отличия от HardwareTesterClient:
    - Использует другой порт по умолчанию (8766 для МК, 8765 для desktop)
    - Добавляет приписку "(МК)" к заголовкам графиков
    - Сохраняет графики с суффиксом _mcu
    
    Пример использования:
    
    >>> # Одновременная работа с desktop и MCU серверами
    >>> desktop_client = HardwareTesterClient(port=8765)
    >>> mcu_client = MCUClient(port=8766)
    >>> 
    >>> await desktop_client.connect()
    >>> await mcu_client.connect()
    >>> 
    >>> desktop_result = await desktop_client.execute("memory_stratification", params)
    >>> mcu_result = await mcu_client.execute("memory_stratification", params)
    >>> 
    >>> desktop_client.plot_memory_stratification(desktop_result)
    >>> mcu_client.plot_memory_stratification(mcu_result)  # Заголовок будет с "(МК)"
    """
    
    DEFAULT_MCU_PORT = 8766  # Другой порт для МК
    SERVER_NAME = "HardwareTester-MCU"
    
    def __init__(self, host: Optional[str] = None, port: int = DEFAULT_MCU_PORT, 
                 img_dir: Optional[str] = None, results_file: Optional[str] = None):
        """
        Инициализация клиента для МК.
        
        Args:
            host: Адрес UART-сервера (по умолчанию localhost)
            port: Порт сервера (по умолчанию 8766)
            img_dir: Папка для сохранения графиков
            results_file: Путь к файлу для сохранения результатов
        """
        # По умолчанию подключаемся к localhost т.к. UART-сервер локальный
        if host is None:
            host = "127.0.0.1"
        super().__init__(host=host, port=port, img_dir=img_dir, results_file=results_file)
        self._mcu_suffix = " (МК)"
        self._file_suffix = "_mcu"
    
    def _get_save_path(self, name: str, save_path: Optional[str] = None) -> Optional[str]:
        """Добавляет суффикс _mcu к имени файла."""
        if save_path:
            # Вставляем суффикс перед расширением
            base, ext = os.path.splitext(save_path)
            return f"{base}{self._file_suffix}{ext}"
        if self.img_dir:
            return os.path.join(self.img_dir, f"{name}{self._file_suffix}.png")
        return None
    
    def _add_mcu_suffix(self, title: str) -> str:
        """Добавляет приписку (МК) к заголовку."""
        if not title.endswith(self._mcu_suffix):
            return title + self._mcu_suffix
        return title
    
    def plot_memory_stratification(self, data: Dict[str, Any], 
                                    save_path: Optional[str] = None,
                                    show: bool = True,
                                    smooth: bool = False,
                                    smooth_window: int = 5) -> plt.Figure:
        """Строит график с приписком (МК)."""
        if "error" in data:
            console.print(f"[red][[-]][/red] Ошибка в данных: {data['error']}")
            return None
        
        data_points = data.get("dataPoints", [])
        analysis = data.get("analysis", {})
        params = data.get("parameters", {})
        
        if not data_points:
            console.print("[red][[-]][/red] Нет данных для построения графика")
            return None
        
        steps = [p["step"] if "step" in p else p.get("size_kb", 0) * 1024 for p in data_points]
        times = [p["time_us"] if "time_us" in p else p.get("time_ns", 0) / 1000 for p in data_points]
        steps_kb = [s / 1024 if s > 100 else s for s in steps]
        
        fig, ax = plt.subplots(figsize=(12, 7), facecolor='white')
        ax.plot(steps_kb, times, 'b-', linewidth=1.5, label='Время доступа')
        
        ax.set_xlabel('Размер (КБ)', fontsize=12)
        ax.set_ylabel('Время (мкс)', fontsize=12)
        ax.set_title(self._add_mcu_suffix('Исследование расслоения динамической памяти'), 
                    fontsize=14, fontweight='bold')
        ax.grid(True, linestyle='--', alpha=0.7)
        
        plt.tight_layout()
        
        actual_save_path = self._get_save_path("exp1_memory_stratification", save_path)
        if actual_save_path:
            fig.savefig(actual_save_path, dpi=150, bbox_inches='tight')
            console.print(f"[green][[+]][/green] График сохранён: {actual_save_path}")
        
        if show:
            plt.show()
            plt.close(fig)
        
        return fig if not show else None
    
    def plot_list_vs_array(self, data: Dict[str, Any],
                           save_path: Optional[str] = None,
                           show: bool = True) -> plt.Figure:
        """Строит график с приписком (МК)."""
        if "error" in data:
            console.print(f"[red][[-]][/red] Ошибка в данных: {data['error']}")
            return None
        
        # Для MCU данные могут быть в другом формате
        elements = data.get("elements", 0)
        array_time = data.get("array_time_us", 0)
        list_time = data.get("list_time_us", 0)
        ratio = data.get("list_to_array_ratio", list_time / array_time if array_time else 0)
        
        # Выводим результаты
        tbl = Table(show_header=False, box=box.SIMPLE, padding=(0, 1))
        tbl.add_column("Key", style="bold")
        tbl.add_column("Value")
        tbl.add_row("Элементов", str(elements))
        tbl.add_row("Время работы со списком", f"{list_time:.2f} мкс")
        tbl.add_row("Время работы с массивом", f"{array_time:.2f} мкс")
        tbl.add_row("Отношение (список/массив)", f"{ratio:.2f}x")
        
        console.print(Panel(tbl, title=self._add_mcu_suffix("ВЫВОДЫ: Сравнение списка и массива"), border_style="green"))
        
        # Простой bar chart для MCU
        fig, ax = plt.subplots(figsize=(10, 6), facecolor='white')
        bars = ax.bar(['Массив', 'Список'], [array_time, list_time], 
                     color=['#2ecc71', '#e74c3c'], width=0.6)
        
        ax.set_ylabel('Время (мкс)', fontsize=12)
        ax.set_title(self._add_mcu_suffix('Сравнение списка и массива'), 
                    fontsize=14, fontweight='bold')
        ax.grid(True, linestyle='--', alpha=0.7, axis='y')
        
        # Добавляем значения на столбцах
        for bar, val in zip(bars, [array_time, list_time]):
            ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5,
                   f'{val:.2f}', ha='center', va='bottom', fontsize=10)
        
        plt.tight_layout()
        
        actual_save_path = self._get_save_path("exp2_list_vs_array", save_path)
        if actual_save_path:
            fig.savefig(actual_save_path, dpi=150, bbox_inches='tight')
            console.print(f"[green][[+]][/green] График сохранён: {actual_save_path}")
        
        if show:
            plt.show()
            plt.close(fig)
        
        return fig if not show else None
    
    def plot_cache_conflicts(self, data: Dict[str, Any],
                             save_path: Optional[str] = None,
                             show: bool = True) -> plt.Figure:
        """Строит график с приписком (МК)."""
        if "error" in data:
            console.print(f"[red][[-]][/red] Ошибка в данных: {data['error']}")
            return None
        
        data_points = data.get("dataPoints", [])
        if not data_points:
            console.print("[red][[-]][/red] Нет данных для построения графика")
            return None
        
        lines = [p["line"] for p in data_points]
        conflict_times = [p["conflict_ns"] for p in data_points]
        no_conflict_times = [p["no_conflict_ns"] for p in data_points]
        
        fig, ax = plt.subplots(figsize=(12, 7), facecolor='white')
        ax.plot(lines, conflict_times, 'r-', linewidth=1.5, label='С конфликтами', marker='o', markersize=4)
        ax.plot(lines, no_conflict_times, 'g-', linewidth=1.5, label='Без конфликтов', marker='s', markersize=4)
        
        ax.set_xlabel('Номер линейки', fontsize=12)
        ax.set_ylabel('Время доступа (нс)', fontsize=12)
        ax.set_title(self._add_mcu_suffix('Влияние конфликтов в кэш-памяти'), 
                    fontsize=14, fontweight='bold')
        ax.grid(True, linestyle='--', alpha=0.7)
        ax.legend(loc='upper right')
        
        plt.tight_layout()
        
        actual_save_path = self._get_save_path("exp5_cache_conflicts", save_path)
        if actual_save_path:
            fig.savefig(actual_save_path, dpi=150, bbox_inches='tight')
            console.print(f"[green][[+]][/green] График сохранён: {actual_save_path}")
        
        if show:
            plt.show()
            plt.close(fig)
        
        return fig if not show else None
    
    def plot_sorting_algorithms(self, data: Dict[str, Any],
                                save_path: Optional[str] = None,
                                show: bool = True) -> plt.Figure:
        """Строит график с приписком (МК)."""
        if "error" in data:
            console.print(f"[red][[-]][/red] Ошибка в данных: {data['error']}")
            return None
        
        elements = data.get("elements", 0)
        algorithms = {
            'Bubble': data.get("bubble_sort_us", 0),
            'Insertion': data.get("insertion_sort_us", 0),
            'Shell': data.get("shell_sort_us", 0),
            'Quick': data.get("quick_sort_us", 0),
        }
        
        # Выводим результаты
        tbl = Table(show_header=False, box=box.SIMPLE, padding=(0, 1))
        tbl.add_column("Алгоритм", style="bold")
        tbl.add_column("Время (мкс)")
        for name, time in algorithms.items():
            tbl.add_row(name, f"{time:.2f}")
        
        console.print(Panel(tbl, title=self._add_mcu_suffix(f"Сортировка {elements} элементов"), border_style="green"))
        
        # Bar chart
        fig, ax = plt.subplots(figsize=(10, 6), facecolor='white')
        colors = ['#e74c3c', '#f39c12', '#3498db', '#2ecc71']
        bars = ax.bar(list(algorithms.keys()), list(algorithms.values()), 
                     color=colors, width=0.6)
        
        ax.set_ylabel('Время (мкс)', fontsize=12)
        ax.set_title(self._add_mcu_suffix(f'Сравнение алгоритмов сортировки ({elements} элементов)'), 
                    fontsize=14, fontweight='bold')
        ax.grid(True, linestyle='--', alpha=0.7, axis='y')
        
        # Добавляем значения на столбцах
        for bar, val in zip(bars, algorithms.values()):
            ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5,
                   f'{val:.0f}', ha='center', va='bottom', fontsize=10)
        
        plt.tight_layout()
        
        actual_save_path = self._get_save_path("exp6_sorting", save_path)
        if actual_save_path:
            fig.savefig(actual_save_path, dpi=150, bbox_inches='tight')
            console.print(f"[green][[+]][/green] График сохранён: {actual_save_path}")
        
        if show:
            plt.show()
            plt.close(fig)
        
        return fig if not show else None
    
    def plot_generic(self, data: Dict[str, Any],
                     x_key: str = "step",
                     y_key: str = "time_us",
                     title: str = "Результаты эксперимента",
                     x_label: str = "X",
                     y_label: str = "Y",
                     save_path: Optional[str] = None,
                     show: bool = True) -> plt.Figure:
        """Строит обобщённый график с приписком (МК)."""
        return super().plot_generic(
            data, x_key, y_key, 
            self._add_mcu_suffix(title), 
            x_label, y_label, save_path, show
        )

