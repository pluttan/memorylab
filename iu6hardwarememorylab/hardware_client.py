"""
IU6 Hardware Memory Lab
========================

Библиотека для подключения к серверу HardwareTester через WebSocket.
Автоматически обнаруживает сервер в локальной сети и позволяет
выполнять функции тестирования с визуализацией результатов.

Кафедра ИУ-6, МГТУ им. Н.Э. Баумана
"""

import asyncio
import json
import socket
import struct
import fcntl
import array
import os
import websockets
from typing import Optional, Dict, Any, List, Tuple
from dataclasses import dataclass
import matplotlib.pyplot as plt
import numpy as np
from rich.console import Console
from rich.panel import Panel
from rich.table import Table
from rich import box

# Глобальный экземпляр консоли для красивого вывода
console = Console()


@dataclass
class ServerInfo:
    """Информация о найденном сервере"""
    host: str
    port: int
    name: str
    version: str


@dataclass  
class NetworkInterface:
    """Информация о сетевом интерфейсе"""
    name: str
    ip: str
    netmask: str
    network: str
    broadcast: str


class HardwareTesterClient:
    """
    Клиент для подключения к серверу HardwareTester.
    
    Примеры использования:
    
    >>> client = HardwareTesterClient()
    >>> await client.connect()
    >>> result = await client.execute("memory_stratification", {"param1": 64, "param2": 4, "param3": 8})
    >>> client.plot_memory_stratification(result)
    """
    
    DEFAULT_PORT = 8765
    SERVER_NAME = "HardwareTester"
    CONNECTION_TIMEOUT = 5
    
    def __init__(self, host: Optional[str] = None, port: int = DEFAULT_PORT, img_dir: Optional[str] = None, results_file: Optional[str] = None):
        """
        Инициализация клиента.
        
        Args:
            host: Адрес сервера (если None, будет выполнен автопоиск)
            port: Порт сервера (по умолчанию 8765)
            img_dir: Папка для сохранения графиков (если None, автосохранение отключено)
            results_file: Путь к файлу для сохранения результатов (если None, сохранение отключено)
        """
        self.host = host
        self.port = port
        self.img_dir = img_dir
        self.results_file = results_file
        self.websocket = None
        self.server_info: Optional[ServerInfo] = None
        self._connected = False
        
        # Создаём папку для изображений если указана
        if self.img_dir:
            os.makedirs(self.img_dir, exist_ok=True)
        
        # Очищаем файл результатов если указан
        if self.results_file:
            with open(self.results_file, 'w', encoding='utf-8') as f:
                f.write("=" * 70 + "\n")
                f.write("РЕЗУЛЬТАТЫ ЭКСПЕРИМЕНТОВ\n")
                f.write("=" * 70 + "\n\n")
    
    def _save_results(self, experiment_name: str, params: Dict[str, Any], conclusions: Dict[str, Any]):
        """
        Сохраняет результаты эксперимента в файл.
        
        Args:
            experiment_name: Название эксперимента
            params: Параметры эксперимента
            conclusions: Выводы эксперимента
        """
        if not self.results_file:
            return
        
        with open(self.results_file, 'a', encoding='utf-8') as f:
            f.write(f"{experiment_name}\n")
            f.write("-" * 50 + "\n")
            f.write("Параметры:\n")
            for key, value in params.items():
                f.write(f"  {key}: {value}\n")
            f.write("\nРезультаты:\n")
            for key, value in conclusions.items():
                if isinstance(value, float):
                    f.write(f"  {key}: {value:.4f}\n")
                else:
                    f.write(f"  {key}: {value}\n")
            f.write("\n" + "=" * 70 + "\n\n")
    

    def _get_save_path(self, name: str, save_path: Optional[str] = None) -> Optional[str]:
        """
        Определяет путь для сохранения графика.
        
        Args:
            name: Имя файла (без расширения)
            save_path: Явно указанный путь (приоритет)
            
        Returns:
            Путь для сохранения или None
        """
        if save_path:
            return save_path
        if self.img_dir:
            return os.path.join(self.img_dir, f"{name}.png")
        return None
    
    def _get_network_interfaces(self) -> List[NetworkInterface]:
        """
        Получает список сетевых интерфейсов с их IP и масками.
        
        Returns:
            Список объектов NetworkInterface
        """
        interfaces = []
        
        try:
            # Получаем список всех интерфейсов
            import netifaces
            for iface_name in netifaces.interfaces():
                addrs = netifaces.ifaddresses(iface_name)
                if netifaces.AF_INET in addrs:
                    for addr_info in addrs[netifaces.AF_INET]:
                        ip = addr_info.get('addr')
                        netmask = addr_info.get('netmask')
                        
                        if ip and netmask and not ip.startswith('127.'):
                            # Вычисляем адрес сети
                            ip_parts = [int(x) for x in ip.split('.')]
                            mask_parts = [int(x) for x in netmask.split('.')]
                            network_parts = [ip_parts[i] & mask_parts[i] for i in range(4)]
                            broadcast_parts = [network_parts[i] | (255 - mask_parts[i]) for i in range(4)]
                            
                            interfaces.append(NetworkInterface(
                                name=iface_name,
                                ip=ip,
                                netmask=netmask,
                                network='.'.join(map(str, network_parts)),
                                broadcast='.'.join(map(str, broadcast_parts))
                            ))
        except ImportError:
            # Fallback если netifaces не установлен
            interfaces = self._get_network_interfaces_fallback()
        
        return interfaces
    
    def _get_network_interfaces_fallback(self) -> List[NetworkInterface]:
        """
        Альтернативный способ получения сетевых интерфейсов без netifaces.
        """
        interfaces = []
        
        try:
            # Получаем IP через подключение к внешнему адресу
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            local_ip = s.getsockname()[0]
            s.close()
            
            # Предполагаем маску /24 для большинства домашних сетей
            ip_parts = [int(x) for x in local_ip.split('.')]
            network_parts = ip_parts[:3] + [0]
            broadcast_parts = ip_parts[:3] + [255]
            
            interfaces.append(NetworkInterface(
                name="default",
                ip=local_ip,
                netmask="255.255.255.0",
                network='.'.join(map(str, network_parts)),
                broadcast='.'.join(map(str, broadcast_parts))
            ))
            
            # Пробуем получить реальную маску через ifconfig/ip
            import subprocess
            try:
                result = subprocess.run(['ifconfig'], capture_output=True, text=True)
                output = result.stdout
                
                # Парсим вывод ifconfig для нахождения маски
                lines = output.split('\n')
                for i, line in enumerate(lines):
                    if local_ip in line:
                        # Ищем netmask в этой или следующих строках
                        for j in range(max(0, i-2), min(len(lines), i+3)):
                            if 'netmask' in lines[j].lower():
                                parts = lines[j].split()
                                for k, part in enumerate(parts):
                                    if part.lower() == 'netmask' and k+1 < len(parts):
                                        netmask_str = parts[k+1]
                                        # Конвертируем hex маску если нужно
                                        if netmask_str.startswith('0x'):
                                            mask_int = int(netmask_str, 16)
                                            netmask = socket.inet_ntoa(struct.pack('>I', mask_int))
                                        else:
                                            netmask = netmask_str
                                        
                                        mask_parts = [int(x) for x in netmask.split('.')]
                                        network_parts = [ip_parts[i] & mask_parts[i] for i in range(4)]
                                        broadcast_parts = [network_parts[i] | (255 - mask_parts[i]) for i in range(4)]
                                        
                                        interfaces[0] = NetworkInterface(
                                            name="default",
                                            ip=local_ip,
                                            netmask=netmask,
                                            network='.'.join(map(str, network_parts)),
                                            broadcast='.'.join(map(str, broadcast_parts))
                                        )
                                        break
            except Exception:
                pass
                
        except Exception as e:
            console.print(f"[yellow][[!]][/yellow] Не удалось определить сетевой интерфейс: {e}")
        
        return interfaces
    
    def _generate_ip_range(self, interface: NetworkInterface) -> List[str]:
        """
        Генерирует список IP-адресов для сканирования на основе маски сети.
        
        Args:
            interface: Информация о сетевом интерфейсе
            
        Returns:
            Список IP-адресов для проверки
        """
        ips = []
        
        network_parts = [int(x) for x in interface.network.split('.')]
        broadcast_parts = [int(x) for x in interface.broadcast.split('.')]
        
        # Вычисляем количество хостов
        def ip_to_int(parts):
            return (parts[0] << 24) + (parts[1] << 16) + (parts[2] << 8) + parts[3]
        
        def int_to_ip(val):
            return f"{(val >> 24) & 0xFF}.{(val >> 16) & 0xFF}.{(val >> 8) & 0xFF}.{val & 0xFF}"
        
        start_ip = ip_to_int(network_parts) + 1
        end_ip = ip_to_int(broadcast_parts)
        
        # Ограничиваем сканирование до 254 хостов для скорости
        max_hosts = min(end_ip - start_ip, 254)
        
        for i in range(max_hosts):
            ips.append(int_to_ip(start_ip + i))
        
        return ips
    
    async def discover_server(self, verbose: bool = True) -> Optional[str]:
        """
        Автоматический поиск сервера HardwareTester.
        Сначала проверяет localhost, затем сканирует локальную сеть.
        
        Args:
            verbose: Выводить информацию о процессе поиска
            
        Returns:
            Адрес сервера или None, если не найден
        """
        # 1. Проверяем localhost
        if verbose:
            console.print("[cyan][[*]][/cyan] Поиск сервера на localhost...")
        if await self._check_server("127.0.0.1"):
            if verbose:
                console.print(f"[green][[+]][/green] Сервер найден на localhost:{self.port}")
            return "127.0.0.1"
        
        # 2. Получаем информацию о сетевых интерфейсах
        if verbose:
            console.print("[cyan][[*]][/cyan] Определение сетевых интерфейсов...")
        
        interfaces = self._get_network_interfaces()
        
        if not interfaces:
            if verbose:
                console.print("[red][[-]][/red] Не удалось определить сетевые интерфейсы")
            return None
        
        # 3. Сканируем каждый интерфейс
        for iface in interfaces:
            if verbose:
                console.print(f"[cyan][[*]][/cyan] Сканирование сети {iface.network}/{iface.netmask} ({iface.name})...")
            
            ip_range = self._generate_ip_range(iface)
            
            if verbose:
                console.print(f"   Диапазон: {ip_range[0]} - {ip_range[-1]} ({len(ip_range)} адресов)")
            
            # Параллельное сканирование с ограничением
            batch_size = 50
            for i in range(0, len(ip_range), batch_size):
                batch = ip_range[i:i+batch_size]
                tasks = [self._check_server(ip) for ip in batch]
                
                results = await asyncio.gather(*tasks, return_exceptions=True)
                
                for j, result in enumerate(results):
                    if result is True:
                        found_ip = batch[j]
                        if verbose:
                            console.print(f"[green][[+]][/green] Сервер найден на {found_ip}:{self.port}")
                        return found_ip
        
        if verbose:
            console.print("[red][[-]][/red] Сервер не найден в локальной сети")
        return None
    
    async def _check_server(self, ip: str) -> bool:
        """
        Проверяет наличие сервера по указанному адресу.
        
        Args:
            ip: IP-адрес для проверки
            
        Returns:
            True если сервер найден
        """
        try:
            uri = f"ws://{ip}:{self.port}"
            async with websockets.connect(uri, open_timeout=0.5, close_timeout=0.5) as ws:
                response = await asyncio.wait_for(ws.recv(), timeout=0.5)
                data = json.loads(response)
                
                if data.get("serverName") == self.SERVER_NAME:
                    return True
        except Exception:
            pass
        return False
    
    async def connect(self, auto_discover: bool = True) -> bool:
        """
        Подключается к серверу HardwareTester.
        
        Args:
            auto_discover: Автоматически искать сервер если host не указан
            
        Returns:
            True если подключение успешно
        """
        if self._connected:
            return True
        
        # Автопоиск сервера если host не указан
        if self.host is None and auto_discover:
            self.host = await self.discover_server()
            if self.host is None:
                raise ConnectionError("Сервер HardwareTester не найден")
        
        try:
            uri = f"ws://{self.host}:{self.port}"
            console.print(f"[cyan][[*]][/cyan] Подключение к {uri}...")
            
            self.websocket = await websockets.connect(uri, ping_interval=None, ping_timeout=None, max_size=16*1024*1024)
            
            # Получаем приветственное сообщение
            response = await self.websocket.recv()
            data = json.loads(response)
            
            self.server_info = ServerInfo(
                host=self.host,
                port=self.port,
                name=data.get("serverName", "Unknown"),
                version=data.get("version", "Unknown")
            )
            
            self._connected = True
            console.print(f"[green][[+]][/green] Подключено к {self.server_info.name} v{self.server_info.version}")
            return True
            
        except Exception as e:
            console.print(f"[red][[-]][/red] Ошибка подключения: {e}")
            self._connected = False
            return False
    
    async def disconnect(self):
        """Отключается от сервера"""
        if self.websocket:
            await self.websocket.close()
            self.websocket = None
            self._connected = False
            console.print("[cyan][[*]][/cyan] Отключено от сервера")
    
    async def get_server_info(self) -> Dict[str, Any]:
        """Получает информацию о сервере."""
        return await self._send_command({"action": "info"})
    
    async def list_functions(self) -> List[Dict[str, str]]:
        """Получает список доступных функций."""
        result = await self._send_command({"action": "list"})
        return result.get("functions", [])
    
    async def execute(self, function_name: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Выполняет функцию на сервере.
        
        Args:
            function_name: Имя функции для выполнения
            params: Параметры функции (опционально)
            
        Returns:
            Результат выполнения функции в формате JSON
        """
        command = {
            "action": "execute",
            "function": function_name,
            "params": params or {}
        }
        
        console.print(f"[cyan][[*]][/cyan] Выполнение функции [bold]'{function_name}'[/bold]...")
        result = await self._send_command(command)
        console.print(f"[green][[+]][/green] Функция выполнена")
        
        return result
    
    async def cancel(self) -> Dict[str, Any]:
        """
        Отменяет текущий выполняющийся эксперимент.
        
        Returns:
            Статус отмены
        """
        console.print("[cyan][[*]][/cyan] Отправка запроса на отмену эксперимента...")
        result = await self._send_command({"action": "cancel"})
        console.print(f"[green][[+]][/green] {result.get('message', 'Запрос отправлен')}")
        return result
    
    async def _send_command(self, command: Dict[str, Any]) -> Dict[str, Any]:
        """Отправляет команду серверу и получает ответ."""
        if not self._connected or not self.websocket:
            raise ConnectionError("Не подключено к серверу")
        
        await self.websocket.send(json.dumps(command))
        
        # Используем цикл с короткими таймаутами для возможности прерывания
        while True:
            try:
                # Короткий таймаут позволяет проверять CancelledError
                response = await asyncio.wait_for(self.websocket.recv(), timeout=0.1)
                return json.loads(response)
            except asyncio.TimeoutError:
                # Таймаут истёк, проверяем не отменена ли задача
                # Если задача отменена, при следующей итерации возникнет CancelledError
                continue
            except asyncio.CancelledError:
                # При прерывании отправляем cancel на сервер, но НЕ закрываем соединение
                console.print("\n[yellow][[!]][/yellow] Прерывание, отправляем cancel на сервер...")
                try:
                    await self.websocket.send(json.dumps({"action": "cancel"}))
                except Exception:
                    pass
                # Выходим, соединение остаётся открытым
                raise
            except KeyboardInterrupt:
                # Jupyter отправляет KeyboardInterrupt при нажатии stop
                console.print("\n[yellow][[!]][/yellow] KeyboardInterrupt, отправляем cancel на сервер...")
                try:
                    # Пытаемся отправить cancel
                    asyncio.get_event_loop().create_task(
                        self.websocket.send(json.dumps({"action": "cancel"}))
                    )
                except Exception:
                    pass
                # Выходим, соединение остаётся открытым
                raise
    
    # ==================== ВИЗУАЛИЗАЦИЯ ====================
    
    def plot_memory_stratification(self, data: Dict[str, Any], 
                                    save_path: Optional[str] = None,
                                    show: bool = True,
                                    smooth: bool = False,
                                    smooth_window: int = 5) -> plt.Figure:
        """
        Строит график исследования расслоения памяти.
        
        Args:
            data: Результат выполнения функции memory_stratification
            save_path: Путь для сохранения графика (опционально)
            show: Показать график на экране
            smooth: Сглаживать данные скользящим средним для уменьшения шума
            smooth_window: Размер окна для сглаживания (по умолчанию 5)
            
        Returns:
            Объект Figure matplotlib
        """
        if "error" in data:
            console.print(f"[red][[-]][/red] Ошибка в данных: {data['error']}")
            return None
        
        data_points = data.get("dataPoints", [])
        analysis = data.get("analysis", {})
        params = data.get("parameters", {})
        
        if not data_points:
            console.print("[red][[-]][/red] Нет данных для построения графика")
            return None
        
        # Вывод выводов
        if analysis:
            t1_step = analysis.get('T1_step_bytes', 0)
            t2_step = analysis.get('T2_step_bytes', 0)
            banks = analysis.get('estimated_banks', 0)
            page_size = analysis.get('estimated_page_size_bytes', 0)
            
            conclusions = Table(show_header=False, box=box.SIMPLE, padding=(0, 1))
            conclusions.add_column("Key", style="bold")
            conclusions.add_column("Value")
            conclusions.add_row("Точка T1 (первый локальный максимум)", f"{t1_step} байт ({t1_step/1024:.1f} КБ)")
            conclusions.add_row("Точка T2 (глобальный максимум)", f"{t2_step} байт ({t2_step/1024:.1f} КБ)")
            conclusions.add_row("Количество банков памяти", str(banks))
            conclusions.add_row("Размер страницы памяти", f"{page_size} байт ({page_size/1024:.1f} КБ)")
            
            console.print(Panel(conclusions, title="ВЫВОДЫ ЭКСПЕРИМЕНТА 1: Исследование расслоения динамической памяти", border_style="green"))
        
        # Сохраняем результаты в файл
        self._save_results(
            "Эксперимент 1: Исследование расслоения динамической памяти",
            params,
            {**analysis} if analysis else {}
        )
        
        steps = [p["step"] for p in data_points]
        times = [p["time_us"] for p in data_points]
        steps_kb = [s / 1024 for s in steps]
        
        # Сглаживание скользящим средним для уменьшения шума
        if smooth and len(times) >= smooth_window:
            times_arr = np.array(times)
            kernel = np.ones(smooth_window) / smooth_window
            times_smoothed = np.convolve(times_arr, kernel, mode='valid')
            # Обрезаем steps_kb чтобы соответствовал сглаженным данным
            offset = (smooth_window - 1) // 2
            steps_kb = steps_kb[offset:offset + len(times_smoothed)]
            times = times_smoothed.tolist()
        
        fig, ax = plt.subplots(figsize=(12, 7), facecolor='white')
        
        ax.plot(steps_kb, times, 'b-', linewidth=1.5, label='Время доступа')
        
        ax.set_xlabel('Шаг чтения (КБ)', fontsize=12)
        ax.set_ylabel('Время (мкс)', fontsize=12)
        ax.set_title('Исследование расслоения динамической памяти', fontsize=14, fontweight='bold')
        ax.grid(True, linestyle='--', alpha=0.7)
        
        info_text = (
            f"Параметры эксперимента:\n"
            f"  • Макс. расстояние: {params.get('param1_kb', '?')} КБ\n"
            f"  • Шаг: {params.get('param2_b', '?')} Б\n"
            f"  • Размер массива: {params.get('param3_mb', '?')} МБ"
        )
        
        props = dict(boxstyle='round', facecolor='wheat', alpha=0.8)
        ax.text(0.98, 0.98, info_text, transform=ax.transAxes, fontsize=9,
                verticalalignment='top', horizontalalignment='right', bbox=props)
        
        plt.tight_layout()
        
        actual_save_path = self._get_save_path("exp1_memory_stratification", save_path)
        if actual_save_path:
            fig.savefig(actual_save_path, dpi=150, bbox_inches='tight')
            console.print(f"[green][[+]][/green] График сохранён: {actual_save_path}")
        
        if show:
            plt.show()
            plt.close(fig)
            return None
        
        return fig
    
    def plot_generic(self, data: Dict[str, Any],
                     x_key: str = "step",
                     y_key: str = "time_us",
                     title: str = "Результаты эксперимента",
                     x_label: str = "X",
                     y_label: str = "Y",
                     save_path: Optional[str] = None,
                     show: bool = True) -> plt.Figure:
        """Строит обобщённый график из данных эксперимента."""
        data_points = data.get("dataPoints", [])
        
        if not data_points:
            console.print("[red][[-]][/red] Нет данных для построения графика")
            return None
        
        x_values = [p.get(x_key, 0) for p in data_points]
        y_values = [p.get(y_key, 0) for p in data_points]
        
        fig, ax = plt.subplots(figsize=(10, 6), facecolor='white')
        ax.plot(x_values, y_values, 'b-', linewidth=1.5, marker='o', markersize=4)
        
        ax.set_xlabel(x_label, fontsize=12)
        ax.set_ylabel(y_label, fontsize=12)
        ax.set_title(title, fontsize=14, fontweight='bold')
        ax.grid(True, linestyle='--', alpha=0.7)
        
        plt.tight_layout()
        
        actual_save_path = self._get_save_path("plot_generic", save_path)
        if actual_save_path:
            fig.savefig(actual_save_path, dpi=150, bbox_inches='tight')
            console.print(f"[green][[+]][/green] График сохранён: {actual_save_path}")
        
        if show:
            plt.show()
            plt.close(fig)
            return None
        
        return fig

    def plot_list_vs_array(self, data: Dict[str, Any],
                           save_path: Optional[str] = None,
                           show: bool = True) -> plt.Figure:
        """Строит график сравнения ссылочных и векторных структур."""
        if "error" in data:
            console.print(f"[red][[-]][/red] Ошибка в данных: {data['error']}")
            return None
        
        data_points = data.get("dataPoints", [])
        if not data_points:
            console.print("[red][[-]][/red] Нет данных для построения графика")
            return None
        
        # Вывод выводов
        conclusions = data.get("conclusions", {})
        if conclusions:
            ratio = conclusions.get("list_to_array_ratio", 0)
            
            tbl = Table(show_header=False, box=box.SIMPLE, padding=(0, 1))
            tbl.add_column("Key", style="bold")
            tbl.add_column("Value")
            tbl.add_row("Время работы со списком", f"{conclusions.get('total_list_time_us', 0):.2f} мкс")
            tbl.add_row("Время работы с массивом", f"{conclusions.get('total_array_time_us', 0):.2f} мкс")
            tbl.add_row("Отношение (список/массив)", f"{ratio:.2f}x")
            
            console.print(Panel(tbl, title="ВЫВОДЫ ЭКСПЕРИМЕНТА 2: Сравнение ссылочных и векторных структур", border_style="green"))
        
        # Сохраняем результаты в файл
        params_exp = data.get("parameters", {})
        self._save_results(
            "Эксперимент 2: Сравнение ссылочных и векторных структур",
            params_exp,
            conclusions if conclusions else {}
        )
        
        frags = [p["fragmentation"] / 1024 for p in data_points]
        list_times = [p["list_time_us"] for p in data_points]
        array_times = [p["array_time_us"] for p in data_points]
        
        fig, ax = plt.subplots(figsize=(12, 7), facecolor='white')
        ax.plot(frags, list_times, 'r-', linewidth=1.5, label='Список')
        ax.plot(frags, array_times, 'g-', linewidth=1.5, label='Массив')
        ax.set_xlabel('Фрагментация (КБ)', fontsize=12)
        ax.set_ylabel('Время (мкс)', fontsize=12)
        ax.set_title('Сравнение эффективности ссылочных и векторных структур', fontsize=14, fontweight='bold')
        ax.grid(True, linestyle='--', alpha=0.7)
        ax.legend(loc='upper right')
        
        # Добавляем вставку с параметрами эксперимента
        params = data.get("parameters", {})
        info_text = (
            f"Параметры эксперимента:\n"
            f"  • Кол-во элементов: {params.get('param1_m', '?')} М\n"
            f"  • Макс. фрагментация: {params.get('param2_kb', '?')} КБ\n"
            f"  • Шаг фрагментации: {params.get('param3_kb', '?')} КБ"
        )
        props = dict(boxstyle='round', facecolor='wheat', alpha=0.8)
        ax.text(0.98, 0.89, info_text, transform=ax.transAxes, fontsize=9,
                verticalalignment='top', horizontalalignment='right', bbox=props)
        
        plt.tight_layout()
        
        actual_save_path = self._get_save_path("exp2_list_vs_array", save_path)
        if actual_save_path:
            fig.savefig(actual_save_path, dpi=150, bbox_inches='tight')
            console.print(f"[green][[+]][/green] График сохранён: {actual_save_path}")
        if show:
            plt.show()
            plt.close(fig)
            return None
        return fig

    def plot_prefetch(self, data: Dict[str, Any],
                      save_path: Optional[str] = None,
                      show: bool = True,
                      smooth: bool = False,
                      smooth_window: int = 5,
                      remove_spikes: bool = False,
                      spike_threshold: float = 0.5) -> plt.Figure:
        """Строит график эффективности предвыборки.
        
        Args:
            remove_spikes: Удалять единичные пики-выбросы (по умолчанию False чтобы показать пики)
            spike_threshold: Порог выброса (0.5 = 50% от среднего соседей)
        """
        if "error" in data:
            console.print(f"[red][[-]][/red] Ошибка в данных: {data['error']}")
            return None
        
        data_points = data.get("dataPoints", [])
        if not data_points:
            console.print("[red][[-]][/red] Нет данных для построения графика")
            return None
        
        # Определяем формат данных (ns или us)
        sample_point = data_points[0]
        is_nanoseconds = "no_prefetch_ns" in sample_point
        
        # Вывод выводов
        conclusions = data.get("conclusions", {})
        if conclusions:
            ratio = conclusions.get("no_prefetch_to_prefetch_ratio", 0)
            
            tbl = Table(show_header=False, box=box.SIMPLE, padding=(0, 1))
            tbl.add_column("Key", style="bold")
            tbl.add_column("Value")
            if is_nanoseconds:
                total_no = conclusions.get('total_no_prefetch_ns', 0)
                total_pf = conclusions.get('total_prefetch_ns', 0)
                tbl.add_row("Время без предвыборки", f"{total_no:.2f} нс")
                tbl.add_row("Время с предвыборкой", f"{total_pf:.2f} нс")
            else:
                tbl.add_row("Время без предвыборки", f"{conclusions.get('total_no_prefetch_us', 0):.2f} мкс")
                tbl.add_row("Время с предвыборкой", f"{conclusions.get('total_prefetch_us', 0):.2f} мкс")
            tbl.add_row("Замедление без предвыборки", f"{ratio:.2f}x")
            
            console.print(Panel(tbl, title="ВЫВОДЫ ЭКСПЕРИМЕНТА 3: Эффективность программной предвыборки", border_style="green"))
        
        # Сохраняем результаты в файл
        params_exp = data.get("parameters", {})
        self._save_results(
            "Эксперимент 3: Эффективность программной предвыборки",
            params_exp,
            conclusions if conclusions else {}
        )
        
        offsets = [p.get("offset", 0) for p in data_points]  # Показываем в байтах
        
        if is_nanoseconds:
            no_prefetch = [p.get("no_prefetch_ns", 0) for p in data_points]
            prefetch = [p.get("prefetch_ns", 0) for p in data_points]
            y_label = 'Время (нс)'
        else:
            no_prefetch = [p.get("no_prefetch_us", p.get("no_prefetch_ns", 0)) for p in data_points]
            prefetch = [p.get("prefetch_us", p.get("prefetch_ns", 0)) for p in data_points]
            y_label = 'Время (мкс)'
        
        # Удаление пиков-выбросов (значения больше 50% от медианы)
        def remove_outlier_spikes(values):
            result = list(values)
            # Несколько проходов пока есть выбросы
            for _ in range(10):
                median_val = np.median(result)
                threshold = median_val * 4.5  
                changed = False
                for i in range(len(result)):
                    if result[i] > threshold:
                        # Заменяем на медиану
                        result[i] = median_val
                        changed = True
                if not changed:
                    break
            return result
        
        if remove_spikes:
            no_prefetch = remove_outlier_spikes(no_prefetch)
            prefetch = remove_outlier_spikes(prefetch)
        
        # Сглаживание скользящим средним для уменьшения шума
        if smooth and len(no_prefetch) >= smooth_window:
            kernel = np.ones(smooth_window) / smooth_window
            no_prefetch_smooth = np.convolve(np.array(no_prefetch), kernel, mode='valid')
            prefetch_smooth = np.convolve(np.array(prefetch), kernel, mode='valid')
            # Обрезаем offsets чтобы соответствовал сглаженным данным
            offset = (smooth_window - 1) // 2
            offsets = offsets[offset:offset + len(no_prefetch_smooth)]
            no_prefetch = no_prefetch_smooth.tolist()
            prefetch = prefetch_smooth.tolist()
        
        fig, ax = plt.subplots(figsize=(12, 7), facecolor='white')
        ax.plot(offsets, no_prefetch, 'r-', linewidth=1.5, label='Без предвыборки')
        ax.plot(offsets, prefetch, 'g-', linewidth=1.5, label='С предвыборкой')
        ax.set_xlabel('Смещение (байт)', fontsize=12)
        ax.set_ylabel(y_label, fontsize=12)
        ax.set_title('Исследование эффективности программной предвыборки', fontsize=14, fontweight='bold')
        ax.grid(True, linestyle='--', alpha=0.7)
        ax.legend(loc='upper left')
        
        # Добавляем вставку с параметрами эксперимента
        params = data.get("parameters", {})
        info_text = (
            f"Параметры эксперимента:\n"
            f"  • Шаг расстояния: {params.get('param1_b', '?')} Б\n"
            f"  • Размер массива: {params.get('param2_kb', '?')} КБ"
        )
        props = dict(boxstyle='round', facecolor='wheat', alpha=0.8)
        ax.text(0.98, 0.98, info_text, transform=ax.transAxes, fontsize=9,
                verticalalignment='top', horizontalalignment='right', bbox=props)
        
        plt.tight_layout()
        
        actual_save_path = self._get_save_path("exp3_prefetch", save_path)
        if actual_save_path:
            fig.savefig(actual_save_path, dpi=150, bbox_inches='tight')
            console.print(f"[green][[+]][/green] График сохранён: {actual_save_path}")
        if show:
            plt.show()
            plt.close(fig)
            return None
        return fig

    def plot_memory_read_optimization(self, data: Dict[str, Any],
                                       save_path: Optional[str] = None,
                                       show: bool = True) -> plt.Figure:
        """Строит график оптимизации чтения памяти."""
        if "error" in data:
            console.print(f"[red][[-]][/red] Ошибка в данных: {data['error']}")
            return None
        
        data_points = data.get("dataPoints", [])
        if not data_points:
            console.print("[red][[-]][/red] Нет данных для построения графика")
            return None
        
        # Вывод выводов
        conclusions = data.get("conclusions", {})
        if conclusions:
            ratio = conclusions.get("separate_to_optimized_ratio", 0)
            
            tbl = Table(show_header=False, box=box.SIMPLE, padding=(0, 1))
            tbl.add_column("Key", style="bold")
            tbl.add_column("Value")
            tbl.add_row("Время с отдельными массивами", f"{conclusions.get('total_separate_time_us', 0):.2f} мкс")
            tbl.add_row("Время с оптимизированным", f"{conclusions.get('total_optimized_time_us', 0):.2f} мкс")
            tbl.add_row("Замедление неоптимизированной структуры", f"{ratio:.2f}x")
            
            console.print(Panel(tbl, title="ВЫВОДЫ ЭКСПЕРИМЕНТА 4: Оптимизация чтения оперативной памяти", border_style="green"))
        
        # Сохраняем результаты в файл
        params_exp = data.get("parameters", {})
        self._save_results(
            "Эксперимент 4: Оптимизация чтения оперативной памяти",
            params_exp,
            conclusions if conclusions else {}
        )
        
        streams = [p["streams"] for p in data_points]
        separate = [p["separate_time_us"] for p in data_points]
        optimized = [p["optimized_time_us"] for p in data_points]
        
        fig, ax = plt.subplots(figsize=(12, 7), facecolor='white')
        ax.plot(streams, separate, 'r-', linewidth=1.5, label='Отдельные массивы')
        ax.plot(streams, optimized, 'g-', linewidth=1.5, label='Оптимизированный массив')
        ax.set_xlabel('Количество потоков данных', fontsize=12)
        ax.set_ylabel('Время (мкс)', fontsize=12)
        ax.set_title('Исследование оптимизации чтения оперативной памяти', fontsize=14, fontweight='bold')
        ax.grid(True, linestyle='--', alpha=0.7)
        ax.legend(loc='upper left')
        
        # Добавляем вставку с параметрами эксперимента
        params = data.get("parameters", {})
        info_text = (
            f"Параметры эксперимента:\n"
            f"  • Размер массива: {params.get('param1_mb', '?')} МБ\n"
            f"  • Кол-во потоков: {params.get('param2_streams', '?')}"
        )
        props = dict(boxstyle='round', facecolor='wheat', alpha=0.8)
        ax.text(0.02, 0.89, info_text, transform=ax.transAxes, fontsize=9,
                verticalalignment='top', horizontalalignment='left', bbox=props)
        
        plt.tight_layout()
        
        actual_save_path = self._get_save_path("exp4_memory_read_optimization", save_path)
        if actual_save_path:
            fig.savefig(actual_save_path, dpi=150, bbox_inches='tight')
            console.print(f"[green][[+]][/green] График сохранён: {actual_save_path}")
        if show:
            plt.show()
            plt.close(fig)
            return None
        return fig

    def plot_cache_conflicts(self, data: Dict[str, Any],
                             save_path: Optional[str] = None,
                             show: bool = True) -> plt.Figure:
        """Строит график конфликтов в кэш-памяти."""
        if "error" in data:
            console.print(f"[red][[-]][/red] Ошибка в данных: {data['error']}")
            return None
        
        data_points = data.get("dataPoints", [])
        if not data_points:
            console.print("[red][[-]][/red] Нет данных для построения графика")
            return None
        
        # Вывод выводов
        conclusions = data.get("conclusions", {})
        if conclusions:
            ratio = conclusions.get("conflict_to_no_conflict_ratio", 0)
            
            tbl = Table(show_header=False, box=box.SIMPLE, padding=(0, 1))
            tbl.add_column("Key", style="bold")
            tbl.add_column("Value")
            tbl.add_row("Время с конфликтами", f"{conclusions.get('total_conflict_time_us', 0):.2f} мкс")
            tbl.add_row("Время без конфликтов", f"{conclusions.get('total_no_conflict_time_us', 0):.2f} мкс")
            tbl.add_row("Замедление при конфликтах", f"{ratio:.2f}x")
            
            console.print(Panel(tbl, title="ВЫВОДЫ ЭКСПЕРИМЕНТА 5: Конфликты в кэш-памяти", border_style="green"))
        
        # Сохраняем результаты в файл
        params_exp = data.get("parameters", {})
        self._save_results(
            "Эксперимент 5: Конфликты в кэш-памяти",
            params_exp,
            conclusions if conclusions else {}
        )
        
        lines = [p["line"] for p in data_points]
        conflict = [p["conflict_time_us"] for p in data_points]
        no_conflict = [p["no_conflict_time_us"] for p in data_points]
        
        fig, ax = plt.subplots(figsize=(12, 7), facecolor='white')
        ax.plot(lines, conflict, 'r-', linewidth=1.5, label='С конфликтами')
        ax.plot(lines, no_conflict, 'g-', linewidth=1.5, label='Без конфликтов')
        ax.set_xlabel('Количество линеек', fontsize=12)
        ax.set_ylabel('Время (мкс)', fontsize=12)
        ax.set_title('Исследование конфликтов в кэш-памяти', fontsize=14, fontweight='bold')
        ax.grid(True, linestyle='--', alpha=0.7)
        ax.legend(loc='upper left')
        
        # Добавляем вставку с параметрами эксперимента
        params = data.get("parameters", {})
        info_text = (
            f"Параметры эксперимента:\n"
            f"  • Размер банка кэш-памяти: {params.get('param1_kb', '?')} КБ\n"
            f"  • Размер линейки кэш-памяти: {params.get('param2_b', '?')} Б\n"
            f"  • Кол-во линеек: {params.get('param3_lines', '?')}"
        )
        props = dict(boxstyle='round', facecolor='wheat', alpha=0.8)
        ax.text(0.02, 0.90, info_text, transform=ax.transAxes, fontsize=9,
                verticalalignment='top', horizontalalignment='left', bbox=props)
        
        plt.tight_layout()
        
        actual_save_path = self._get_save_path("exp5_cache_conflicts", save_path)
        if actual_save_path:
            fig.savefig(actual_save_path, dpi=150, bbox_inches='tight')
            console.print(f"[green][[+]][/green] График сохранён: {actual_save_path}")
        if show:
            plt.show()
            plt.close(fig)
            return None
        return fig

    def plot_sorting_algorithms(self, data: Dict[str, Any],
                                save_path: Optional[str] = None,
                                show: bool = True) -> plt.Figure:
        """Строит график сравнения алгоритмов сортировки."""
        if "error" in data:
            console.print(f"[red][[-]][/red] Ошибка в данных: {data['error']}")
            return None
        
        data_points = data.get("dataPoints", [])
        if not data_points:
            console.print("[red][[-]][/red] Нет данных для построения графика")
            return None
        
        # Вывод выводов
        conclusions = data.get("conclusions", {})
        if conclusions:
            quick_to_radix = conclusions.get("quicksort_to_radix_ratio", 0)
            quick_to_radix_opt = conclusions.get("quicksort_to_radix_opt_ratio", 0)
            
            tbl = Table(show_header=False, box=box.SIMPLE, padding=(0, 1))
            tbl.add_column("Key", style="bold")
            tbl.add_column("Value")
            tbl.add_row("Время QuickSort", f"{conclusions.get('total_quicksort_us', 0):.2f} мкс")
            tbl.add_row("Время Radix-Counting", f"{conclusions.get('total_radix_us', 0):.2f} мкс")
            tbl.add_row("Время Radix-Counting (оптим.)", f"{conclusions.get('total_radix_opt_us', 0):.2f} мкс")
            tbl.add_row("QuickSort vs Radix-Counting", f"{quick_to_radix:.2f}x")
            tbl.add_row("QuickSort vs Radix (оптим.)", f"{quick_to_radix_opt:.2f}x")
            
            console.print(Panel(tbl, title="ВЫВОДЫ ЭКСПЕРИМЕНТА 6: Сравнение алгоритмов сортировки", border_style="green"))
        
        # Сохраняем результаты в файл
        params_exp = data.get("parameters", {})
        self._save_results(
            "Эксперимент 6: Сравнение алгоритмов сортировки",
            params_exp,
            conclusions if conclusions else {}
        )
        
        elements = [p["elements"] / (1024 * 1024) for p in data_points]
        quicksort = [p["quicksort_time_us"] for p in data_points]
        radix = [p["radix_time_us"] for p in data_points]
        radix_opt = [p.get("radix_opt_time_us", 0) for p in data_points]
        
        fig, ax = plt.subplots(figsize=(12, 7), facecolor='white')
        ax.plot(elements, quicksort, 'm-', linewidth=1.5, label='QuickSort')
        ax.plot(elements, radix, 'b-', linewidth=1.5, label='Radix-Counting Sort')
        ax.plot(elements, radix_opt, 'g-', linewidth=1.5, label='Radix-Counting Sort (оптим.)')
        ax.set_xlabel('Количество элементов (М)', fontsize=12)
        ax.set_ylabel('Время (мкс)', fontsize=12)
        ax.set_title('Сравнение алгоритмов сортировки', fontsize=14, fontweight='bold')
        ax.grid(True, linestyle='--', alpha=0.7)
        ax.legend(loc='upper left')
        
        # Добавляем вставку с параметрами эксперимента
        params = data.get("parameters", {})
        info_text = (
            f"Параметры эксперимента:\n"
            f"  • Кол-во элементов: {params.get('param1_m', '?')} М\n"
            f"  • Шаг увеличения: {params.get('param2_k', '?')} К"
        )
        props = dict(boxstyle='round', facecolor='wheat', alpha=0.8)
        ax.text(0.02, 0.84, info_text, transform=ax.transAxes, fontsize=9,
                verticalalignment='top', horizontalalignment='left', bbox=props)
        
        plt.tight_layout()
        
        actual_save_path = self._get_save_path("exp6_sorting_algorithms", save_path)
        if actual_save_path:
            fig.savefig(actual_save_path, dpi=150, bbox_inches='tight')
            console.print(f"[green][[+]][/green] График сохранён: {actual_save_path}")
        if show:
            plt.show()
            plt.close(fig)
            return None
        return fig


# ==================== УТИЛИТЫ ====================

async def run_experiment(function_name: str, 
                         params: Optional[Dict[str, Any]] = None,
                         host: Optional[str] = None,
                         port: int = 8765,
                         plot: bool = True,
                         save_path: Optional[str] = None) -> Dict[str, Any]:
    """
    Удобная функция для запуска эксперимента.
    
    Args:
        function_name: Имя функции
        params: Параметры
        host: Адрес сервера (автопоиск если None)
        port: Порт сервера
        plot: Строить график
        save_path: Путь для сохранения графика
        
    Returns:
        Результат эксперимента
    """
    client = HardwareTesterClient(host=host, port=port)
    
    try:
        await client.connect()
        result = await client.execute(function_name, params)
        
        if plot and function_name == "memory_stratification":
            client.plot_memory_stratification(result, save_path=save_path)
        elif plot:
            client.plot_generic(result, save_path=save_path)
        
        return result
        
    finally:
        await client.disconnect()
