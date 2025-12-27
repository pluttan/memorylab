#!/usr/bin/env python3
"""
WebSocket-сервер для связи с микроконтроллером через UART.

Этот сервер:
1. Подключается к МК через последовательный порт (UART)
2. Предоставляет WebSocket интерфейс для клиента (hardware_client.py)
3. Транслирует команды клиента в UART-команды для МК
4. Парсит JSON-ответы от МК и отправляет их клиенту

Использование:
    python -m iu6hardwarememorylab.uart_server --port /dev/ttyUSB0
    python -m iu6hardwarememorylab.uart_server --port COM3 --baud 115200
"""

import asyncio
import json
import sys
import argparse
import glob
import time
import threading
from pathlib import Path

try:
    import websockets
    WEBSOCKETS_AVAILABLE = True
except ImportError:
    WEBSOCKETS_AVAILABLE = False

try:
    import serial
    import serial.tools.list_ports
    SERIAL_AVAILABLE = True
except ImportError:
    SERIAL_AVAILABLE = False

try:
    from rich.console import Console
    from rich.panel import Panel
    from rich.table import Table
    from rich.prompt import Prompt
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False

# Конфигурация
DEFAULT_WS_PORT = 8765
DEFAULT_BAUD_RATE = 115200
SERVER_NAME = "HardwareTester-MCU"
SERVER_VERSION = "1.0.0"


class UARTBridge:
    """Мост между UART и WebSocket"""
    
    def __init__(self, serial_port: str, baud_rate: int = DEFAULT_BAUD_RATE):
        self.serial_port = serial_port
        self.baud_rate = baud_rate
        self.serial = None
        self.connected = False
        self.response_buffer = ""
        self.lock = threading.Lock()
    
    def connect(self) -> bool:
        """Подключается к последовательному порту"""
        try:
            self.serial = serial.Serial(
                port=self.serial_port,
                baudrate=self.baud_rate,
                timeout=0.1,
                write_timeout=1
            )
            self.connected = True
            # Ждём инициализации МК
            time.sleep(2)
            # Очищаем буфер
            self.serial.reset_input_buffer()
            return True
        except Exception as e:
            print(f"[UART] Ошибка подключения: {e}")
            return False
    
    def disconnect(self):
        """Отключается от порта"""
        if self.serial and self.serial.is_open:
            self.serial.close()
        self.connected = False
    
    def send_command(self, cmd: str) -> bool:
        """Отправляет команду на МК"""
        if not self.connected or not self.serial:
            return False
        
        try:
            with self.lock:
                # Отправляем команду (один символ для меню МК)
                self.serial.write(cmd.encode('utf-8'))
                self.serial.flush()
            return True
        except Exception as e:
            print(f"[UART] Ошибка отправки: {e}")
            return False
    
    def read_response(self, timeout: float = 30.0) -> str:
        """
        Читает ответ от МК до получения полного JSON объекта.
        """
        if not self.connected or not self.serial:
            return '{"error": "Not connected"}'
        
        start_time = time.time()
        buffer = ""
        json_started = False
        brace_count = 0
        
        while time.time() - start_time < timeout:
            try:
                if self.serial.in_waiting > 0:
                    data = self.serial.read(self.serial.in_waiting)
                    text = data.decode('utf-8', errors='ignore')
                    buffer += text
                    
                    # Ищем начало JSON
                    for char in text:
                        if char == '{':
                            if not json_started:
                                json_started = True
                                # Обрезаем всё до первой {
                                idx = buffer.find('{')
                                buffer = buffer[idx:]
                            brace_count += 1
                        elif char == '}':
                            brace_count -= 1
                        
                        # JSON завершён
                        if json_started and brace_count == 0:
                            # Ищем конец JSON в буфере
                            depth = 0
                            for i, c in enumerate(buffer):
                                if c == '{':
                                    depth += 1
                                elif c == '}':
                                    depth -= 1
                                    if depth == 0:
                                        json_str = buffer[:i+1]
                                        try:
                                            # Проверяем валидность JSON
                                            json.loads(json_str)
                                            return json_str
                                        except json.JSONDecodeError:
                                            # Продолжаем читать
                                            break
                else:
                    time.sleep(0.01)
            except Exception as e:
                print(f"[UART] Ошибка чтения: {e}")
                break
        
        # Таймаут — возвращаем что есть
        if buffer:
            # Пытаемся извлечь JSON
            try:
                start = buffer.find('{')
                if start >= 0:
                    end = buffer.rfind('}')
                    if end > start:
                        json_str = buffer[start:end+1]
                        json.loads(json_str)  # Проверка
                        return json_str
            except:
                pass
        
        return '{"error": "Timeout waiting for response", "partial": "' + buffer[:200].replace('"', '\\"') + '"}'
    
    def get_available_experiments(self) -> list:
        """Возвращает список доступных экспериментов МК"""
        return [
            {"name": "memory_stratification", "description": "Исследование расслоения памяти. Команда: 1"},
            {"name": "list_vs_array", "description": "Сравнение списка и массива. Команда: 2"},
            {"name": "prefetch", "description": "Исследование предвыборки. Команда: 3"},
            {"name": "memory_read_optimization", "description": "Оптимизация чтения памяти. Команда: 4"},
            {"name": "cache_conflicts", "description": "Конфликты в кэш-памяти. Команда: 5"},
            {"name": "sorting_algorithms", "description": "Сравнение алгоритмов сортировки. Команда: 6"},
        ]


class MCUWebSocketServer:
    """WebSocket сервер для МК"""
    
    def __init__(self, uart_bridge: UARTBridge, ws_port: int = DEFAULT_WS_PORT):
        self.bridge = uart_bridge
        self.ws_port = ws_port
        self.clients = set()
    
    async def handle_client(self, websocket, path=None):
        """Обработчик WebSocket соединения"""
        self.clients.add(websocket)
        client_info = f"{websocket.remote_address[0]}:{websocket.remote_address[1]}"
        print(f"[WS] Клиент подключен: {client_info}")
        
        # Отправляем приветствие
        welcome = {
            "type": "welcome",
            "serverName": SERVER_NAME,
            "version": SERVER_VERSION,
            "message": "Connected to MCU via UART",
            "uart_port": self.bridge.serial_port,
            "baud_rate": self.bridge.baud_rate
        }
        await websocket.send(json.dumps(welcome))
        
        try:
            async for message in websocket:
                response = await self.process_command(message)
                await websocket.send(response)
        except websockets.exceptions.ConnectionClosed:
            print(f"[WS] Клиент отключен: {client_info}")
        finally:
            self.clients.discard(websocket)
    
    async def process_command(self, message: str) -> str:
        """Обрабатывает команду от клиента"""
        try:
            cmd = json.loads(message)
        except json.JSONDecodeError:
            return json.dumps({"error": "Invalid JSON"})
        
        action = cmd.get("action", "")
        
        if action == "list":
            # Список функций
            functions = self.bridge.get_available_experiments()
            return json.dumps({"functions": functions})
        
        elif action == "info":
            # Информация о сервере
            return json.dumps({
                "serverName": SERVER_NAME,
                "version": SERVER_VERSION,
                "port": self.ws_port,
                "uart_port": self.bridge.serial_port,
                "connected": self.bridge.connected
            })
        
        elif action == "execute":
            # Выполнение эксперимента
            func_name = cmd.get("function", "")
            
            # Маппинг имён функций на команды МК
            cmd_map = {
                "memory_stratification": "1",
                "list_vs_array": "2",
                "prefetch": "3",
                "memory_read_optimization": "4",
                "cache_conflicts": "5",
                "sorting_algorithms": "6",
                "all": "a",
            }
            
            uart_cmd = cmd_map.get(func_name)
            if not uart_cmd:
                return json.dumps({"error": f"Unknown function: {func_name}"})
            
            print(f"[MCU] Отправка команды: {uart_cmd} ({func_name})")
            
            # Отправляем команду на МК
            if not self.bridge.send_command(uart_cmd):
                return json.dumps({"error": "Failed to send command to MCU"})
            
            # Читаем ответ (с большим таймаутом для длительных экспериментов)
            timeout = cmd.get("params", {}).get("timeout", 60)
            response = await asyncio.get_event_loop().run_in_executor(
                None, lambda: self.bridge.read_response(timeout)
            )
            
            print(f"[MCU] Ответ получен: {len(response)} байт")
            return response
        
        elif action == "cancel":
            # Отмена — отправляем Ctrl+C
            self.bridge.send_command('\x03')
            return json.dumps({"status": "cancelling", "message": "Cancel sent to MCU"})
        
        elif action == "raw":
            # Сырая команда UART
            raw_cmd = cmd.get("command", "")
            if raw_cmd:
                self.bridge.send_command(raw_cmd)
                response = await asyncio.get_event_loop().run_in_executor(
                    None, lambda: self.bridge.read_response(10)
                )
                return response
            return json.dumps({"error": "No command specified"})
        
        else:
            return json.dumps({"error": f"Unknown action: {action}"})
    
    async def start(self):
        """Запускает WebSocket сервер"""
        print(f"\n{'='*50}")
        print(f" {SERVER_NAME} v{SERVER_VERSION}")
        print(f"{'='*50}")
        print(f" UART Port: {self.bridge.serial_port}")
        print(f" Baud Rate: {self.bridge.baud_rate}")
        print(f" WebSocket: ws://localhost:{self.ws_port}")
        print(f"{'='*50}")
        print(" Ожидание подключений...")
        print()
        
        async with websockets.serve(self.handle_client, "0.0.0.0", self.ws_port):
            await asyncio.Future()  # Бесконечное ожидание


def get_serial_ports():
    """Получает список доступных портов"""
    if SERIAL_AVAILABLE:
        ports = list(serial.tools.list_ports.comports())
        return [(p.device, p.description) for p in ports]
    else:
        # Fallback
        ports = []
        for pattern in ['/dev/tty.usb*', '/dev/ttyUSB*', '/dev/ttyACM*', '/dev/cu.usb*']:
            ports.extend(glob.glob(pattern))
        return [(p, p) for p in sorted(set(ports))]


def select_port_interactive():
    """Интерактивный выбор порта"""
    ports = get_serial_ports()
    
    if not ports:
        print("Не найдено ни одного последовательного порта!")
        print("Подключите устройство и попробуйте снова.")
        return None
    
    if len(ports) == 1:
        print(f"Найден порт: {ports[0][0]} ({ports[0][1]})")
        return ports[0][0]
    
    if RICH_AVAILABLE:
        console = Console()
        console.print()
        console.print(Panel.fit("[bold yellow]Доступные порты[/bold yellow]"))
        
        table = Table(show_header=True, header_style="bold magenta")
        table.add_column("№", style="dim", width=3)
        table.add_column("Порт", style="cyan")
        table.add_column("Описание", style="dim")
        
        for i, (port, desc) in enumerate(ports, 1):
            table.add_row(str(i), port, desc)
        
        console.print(table)
        
        choices = [str(i) for i in range(1, len(ports) + 1)]
        choice = Prompt.ask(
            "[bold green]Выберите номер порта[/bold green]",
            choices=choices,
            default="1"
        )
    else:
        print("\nДоступные порты:")
        for i, (port, desc) in enumerate(ports, 1):
            print(f"  {i}. {port} - {desc}")
        
        while True:
            choice = input(f"Выберите номер порта [1]: ").strip()
            if not choice:
                choice = "1"
            if choice.isdigit() and 1 <= int(choice) <= len(ports):
                break
            print(f"Неверный выбор. Введите число от 1 до {len(ports)}")
    
    return ports[int(choice) - 1][0]


def main():
    """Главная функция"""
    # Проверяем зависимости
    if not WEBSOCKETS_AVAILABLE:
        print("Ошибка: Библиотека websockets не установлена")
        print("Установите: pip install websockets")
        return 1
    
    if not SERIAL_AVAILABLE:
        print("Ошибка: Библиотека pyserial не установлена")
        print("Установите: pip install pyserial")
        return 1
    
    parser = argparse.ArgumentParser(
        description="WebSocket сервер для связи с МК через UART"
    )
    parser.add_argument(
        "-p", "--port",
        help="Последовательный порт (например /dev/ttyUSB0 или COM3)"
    )
    parser.add_argument(
        "-b", "--baud",
        type=int,
        default=DEFAULT_BAUD_RATE,
        help=f"Скорость порта (по умолчанию {DEFAULT_BAUD_RATE})"
    )
    parser.add_argument(
        "-w", "--ws-port",
        type=int,
        default=DEFAULT_WS_PORT,
        help=f"Порт WebSocket сервера (по умолчанию {DEFAULT_WS_PORT})"
    )
    
    args = parser.parse_args()
    
    # Выбор порта
    if args.port:
        serial_port = args.port
    else:
        serial_port = select_port_interactive()
        if not serial_port:
            return 1
    
    # Создаём мост UART
    bridge = UARTBridge(serial_port, args.baud)
    
    print(f"\n[UART] Подключение к {serial_port}...")
    if not bridge.connect():
        print("[UART] Не удалось подключиться!")
        return 1
    
    print("[UART] Подключено!")
    
    # Запускаем WebSocket сервер
    server = MCUWebSocketServer(bridge, args.ws_port)
    
    try:
        asyncio.run(server.start())
    except KeyboardInterrupt:
        print("\n[Server] Остановка...")
    finally:
        bridge.disconnect()
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
