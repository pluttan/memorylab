#!/usr/bin/env python3
"""
Интерактивный выбор микроконтроллера и порта для PlatformIO
"""

import os
import sys
import subprocess
import glob
from pathlib import Path

try:
    from rich.console import Console
    from rich.panel import Panel
    from rich.table import Table
    from rich.prompt import Prompt, Confirm
    from rich.progress import Progress, SpinnerColumn, TextColumn
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False

# Доступные окружения PlatformIO
ENVIRONMENTS = {
    "1": {
        "env": "esp32",
        "name": "ESP32 DevKit",
        "platform": "Espressif32",
        "ram": "320 KB",
        "cache": "Да",
        "description": "Популярный модуль с WiFi/BT, достаточно RAM"
    },
    "2": {
        "env": "esp32s3",
        "name": "ESP32-S3 DevKit",
        "platform": "Espressif32",
        "ram": "512 KB+",
        "cache": "Да",
        "description": "Новейший ESP с PSRAM, много памяти"
    },
    "3": {
        "env": "esp8266",
        "name": "NodeMCU ESP8266",
        "platform": "Espressif8266",
        "ram": "80 KB",
        "cache": "Нет",
        "description": "Бюджетный WiFi модуль"
    },
    "4": {
        "env": "stm32f103",
        "name": "Blue Pill (STM32F103)",
        "platform": "STM32",
        "ram": "20 KB",
        "cache": "Нет",
        "description": "Дешёвая плата STM32, мало памяти"
    },
    "5": {
        "env": "stm32f401",
        "name": "Black Pill (STM32F401)",
        "platform": "STM32",
        "ram": "96 KB",
        "cache": "Нет",
        "description": "Плата WeAct с USB-C, хороший баланс"
    },
    "6": {
        "env": "stm32f411",
        "name": "Black Pill (STM32F411)",
        "platform": "STM32",
        "ram": "128 KB",
        "cache": "Нет",
        "description": "Улучшенная версия F401"
    },
    "7": {
        "env": "nucleo_f446re",
        "name": "Nucleo F446RE",
        "platform": "STM32",
        "ram": "128 KB",
        "cache": "Нет",
        "description": "Официальная отладочная плата ST"
    },
    "8": {
        "env": "nucleo_f746zg",
        "name": "Nucleo F746ZG",
        "platform": "STM32",
        "ram": "320 KB",
        "cache": "Да (L1)",
        "description": "Cortex-M7 с кэшем, все эксперименты актуальны"
    },
    "9": {
        "env": "uno",
        "name": "Arduino Uno",
        "platform": "Atmel AVR",
        "ram": "2 KB",
        "cache": "Нет",
        "description": "Классическая плата, очень мало RAM"
    },
    "10": {
        "env": "mega",
        "name": "Arduino Mega",
        "platform": "Atmel AVR",
        "ram": "8 KB",
        "cache": "Нет",
        "description": "Больше пинов и памяти, чем Uno"
    },
    "11": {
        "env": "nano",
        "name": "Arduino Nano",
        "platform": "Atmel AVR",
        "ram": "2 KB",
        "cache": "Нет",
        "description": "Компактная версия Uno"
    },
    "12": {
        "env": "pico",
        "name": "Raspberry Pi Pico",
        "platform": "RP2040",
        "ram": "264 KB",
        "cache": "XIP",
        "description": "Двухъядерный ARM, много памяти, дешёвый"
    },
}


def get_serial_ports():
    """Получает список доступных серийных портов"""
    ports = []
    
    # macOS
    ports.extend(glob.glob('/dev/tty.usbserial*'))
    ports.extend(glob.glob('/dev/tty.usbmodem*'))
    ports.extend(glob.glob('/dev/tty.wchusbserial*'))
    ports.extend(glob.glob('/dev/cu.usbserial*'))
    ports.extend(glob.glob('/dev/cu.usbmodem*'))
    ports.extend(glob.glob('/dev/cu.wchusbserial*'))
    ports.extend(glob.glob('/dev/cu.SLAB_USBtoUART*'))
    
    # Linux
    ports.extend(glob.glob('/dev/ttyUSB*'))
    ports.extend(glob.glob('/dev/ttyACM*'))
    ports.extend(glob.glob('/dev/ttyAMA*'))
    
    # Удаляем дубликаты и сортируем
    return sorted(set(ports))


def print_simple_menu():
    """Простой вывод меню без rich"""
    print("\n" + "=" * 60)
    print(" Выбор микроконтроллера для Memory Lab")
    print("=" * 60)
    print()
    print(f"{'№':>2} | {'Плата':<25} | {'RAM':<10} | {'Кэш':<8}")
    print("-" * 60)
    
    for key, info in ENVIRONMENTS.items():
        print(f"{key:>2} | {info['name']:<25} | {info['ram']:<10} | {info['cache']:<8}")
    
    print("-" * 60)
    print()


def print_rich_menu(console):
    """Красивый вывод меню с rich"""
    console.print()
    console.print(Panel.fit(
        "[bold cyan]Выбор микроконтроллера для Memory Lab[/bold cyan]",
        border_style="cyan"
    ))
    console.print()
    
    table = Table(show_header=True, header_style="bold magenta")
    table.add_column("№", style="dim", width=3)
    table.add_column("Плата", style="cyan", width=25)
    table.add_column("Платформа", width=15)
    table.add_column("RAM", width=10)
    table.add_column("Кэш", width=8)
    table.add_column("Описание", style="dim")
    
    for key, info in ENVIRONMENTS.items():
        cache_style = "green" if info['cache'] != "Нет" else "dim"
        table.add_row(
            key,
            info['name'],
            info['platform'],
            info['ram'],
            f"[{cache_style}]{info['cache']}[/{cache_style}]",
            info['description']
        )
    
    console.print(table)
    console.print()


def print_port_menu(ports, console=None):
    """Выводит меню портов"""
    if not ports:
        msg = "Не найдено ни одного порта! Подключите устройство."
        if console:
            console.print(f"[bold red]{msg}[/bold red]")
        else:
            print(msg)
        return None
    
    if console:
        console.print()
        console.print(Panel.fit(
            "[bold yellow]Доступные порты[/bold yellow]",
            border_style="yellow"
        ))
        
        table = Table(show_header=True, header_style="bold magenta")
        table.add_column("№", style="dim", width=3)
        table.add_column("Порт", style="cyan")
        
        for i, port in enumerate(ports, 1):
            table.add_row(str(i), port)
        
        console.print(table)
        console.print()
    else:
        print("\nДоступные порты:")
        print("-" * 40)
        for i, port in enumerate(ports, 1):
            print(f"  {i}. {port}")
        print("-" * 40)
        print()
    
    return ports


def select_port(ports):
    """Интерактивный выбор порта"""
    if not ports:
        return None
    
    if len(ports) == 1:
        # Единственный порт — используем его
        if RICH_AVAILABLE:
            console = Console()
            console.print(f"[green]Найден единственный порт:[/green] {ports[0]}")
        else:
            print(f"Найден единственный порт: {ports[0]}")
        return ports[0]
    
    if RICH_AVAILABLE:
        console = Console()
        print_port_menu(ports, console)
        
        choices = [str(i) for i in range(1, len(ports) + 1)]
        choice = Prompt.ask(
            "[bold green]Выберите номер порта[/bold green]",
            choices=choices,
            default="1"
        )
    else:
        print_port_menu(ports)
        
        while True:
            choice = input(f"Выберите номер порта [1]: ").strip()
            if not choice:
                choice = "1"
            if choice.isdigit() and 1 <= int(choice) <= len(ports):
                break
            print(f"Неверный выбор. Введите число от 1 до {len(ports)}")
    
    return ports[int(choice) - 1]


def select_environment():
    """Интерактивный выбор окружения"""
    if RICH_AVAILABLE:
        console = Console()
        print_rich_menu(console)
        
        choice = Prompt.ask(
            "[bold green]Выберите номер платы[/bold green]",
            choices=list(ENVIRONMENTS.keys()),
            default="1"
        )
    else:
        print_simple_menu()
        
        while True:
            choice = input("Выберите номер платы [1]: ").strip()
            if not choice:
                choice = "1"
            if choice in ENVIRONMENTS:
                break
            print(f"Неверный выбор. Введите число от 1 до {len(ENVIRONMENTS)}")
    
    selected = ENVIRONMENTS[choice]
    
    if RICH_AVAILABLE:
        console.print()
        console.print(Panel(
            f"[bold green]Выбрано:[/bold green] {selected['name']}\n"
            f"[dim]Окружение PlatformIO:[/dim] {selected['env']}",
            border_style="green"
        ))
    else:
        print()
        print(f"Выбрано: {selected['name']}")
        print(f"Окружение PlatformIO: {selected['env']}")
    
    return selected['env']


def run_build(env, hardware_mc_dir):
    """Запускает сборку"""
    cmd = ["pio", "run", "-e", env]
    
    if RICH_AVAILABLE:
        console = Console()
        console.print()
        console.print(Panel.fit(
            f"[bold cyan]Сборка прошивки[/bold cyan]\n"
            f"[dim]Окружение:[/dim] {env}",
            border_style="cyan"
        ))
        console.print()
    else:
        print(f"\n{'='*50}")
        print(f" Сборка прошивки (env: {env})")
        print(f"{'='*50}\n")
    
    ret = subprocess.call(cmd, cwd=hardware_mc_dir)
    
    if ret == 0:
        if RICH_AVAILABLE:
            console.print()
            console.print("[bold green]✓ Сборка успешно завершена![/bold green]")
        else:
            print("\n✓ Сборка успешно завершена!")
    else:
        if RICH_AVAILABLE:
            console.print()
            console.print("[bold red]✗ Ошибка сборки![/bold red]")
        else:
            print("\n✗ Ошибка сборки!")
    
    return ret


def run_upload(env, port, hardware_mc_dir):
    """Запускает загрузку прошивки"""
    cmd = ["pio", "run", "-e", env, "-t", "upload"]
    if port:
        cmd.extend(["--upload-port", port])
    
    if RICH_AVAILABLE:
        console = Console()
        console.print()
        console.print(Panel.fit(
            f"[bold yellow]Загрузка прошивки[/bold yellow]\n"
            f"[dim]Окружение:[/dim] {env}\n"
            f"[dim]Порт:[/dim] {port or 'авто'}",
            border_style="yellow"
        ))
        console.print()
    else:
        print(f"\n{'='*50}")
        print(f" Загрузка прошивки")
        print(f" Окружение: {env}")
        print(f" Порт: {port or 'авто'}")
        print(f"{'='*50}\n")
    
    ret = subprocess.call(cmd, cwd=hardware_mc_dir)
    
    if ret == 0:
        if RICH_AVAILABLE:
            console.print()
            console.print("[bold green]✓ Загрузка успешно завершена![/bold green]")
        else:
            print("\n✓ Загрузка успешно завершена!")
    else:
        if RICH_AVAILABLE:
            console.print()
            console.print("[bold red]✗ Ошибка загрузки![/bold red]")
        else:
            print("\n✗ Ошибка загрузки!")
    
    return ret


def run_monitor(port, hardware_mc_dir):
    """Запускает UART монитор"""
    cmd = ["pio", "device", "monitor", "-b", "115200"]
    if port:
        cmd.extend(["-p", port])
    
    if RICH_AVAILABLE:
        console = Console()
        console.print()
        console.print(Panel.fit(
            f"[bold magenta]UART Monitor[/bold magenta]\n"
            f"[dim]Порт:[/dim] {port or 'авто'}\n"
            f"[dim]Скорость:[/dim] 115200 baud\n"
            f"[dim]Выход:[/dim] Ctrl+C",
            border_style="magenta"
        ))
        console.print()
    else:
        print(f"\n{'='*50}")
        print(f" UART Monitor")
        print(f" Порт: {port or 'авто'}")
        print(f" Выход: Ctrl+C")
        print(f"{'='*50}\n")
    
    return subprocess.call(cmd, cwd=hardware_mc_dir)


def interactive_upload_loop(hardware_mc_dir, initial_env=None, initial_port=None):
    """
    Интерактивный цикл загрузки с повтором при ошибке.
    
    При ошибке загрузки:
    - Возврат к выбору МК
    - Если МК тот же — сразу к выбору порта
    - Если порт тот же — показ ошибки и повтор
    """
    console = Console() if RICH_AVAILABLE else None
    
    last_env = initial_env
    last_port = initial_port
    last_build_env = None  # Для какого env была последняя успешная сборка
    
    while True:
        # === Выбор МК ===
        if last_env is None:
            env = select_environment()
        else:
            # Спрашиваем: использовать предыдущий МК?
            if RICH_AVAILABLE:
                console.print()
                use_same = Confirm.ask(
                    f"[yellow]Использовать тот же МК ({last_env})?[/yellow]",
                    default=True
                )
            else:
                answer = input(f"Использовать тот же МК ({last_env})? [Y/n]: ").strip().lower()
                use_same = answer in ('', 'y', 'yes', 'д', 'да')
            
            if use_same:
                env = last_env
                if RICH_AVAILABLE:
                    console.print(f"[green]Используется:[/green] {env}")
                else:
                    print(f"Используется: {env}")
            else:
                env = select_environment()
                last_port = None  # Сбрасываем порт при смене МК
        
        # === Сборка (только если МК изменился) ===
        if last_build_env != env:
            ret = run_build(env, hardware_mc_dir)
            if ret != 0:
                if RICH_AVAILABLE:
                    console.print()
                    retry = Confirm.ask("[red]Ошибка сборки. Попробовать снова?[/red]", default=True)
                else:
                    answer = input("Ошибка сборки. Попробовать снова? [Y/n]: ").strip().lower()
                    retry = answer in ('', 'y', 'yes', 'д', 'да')
                
                if retry:
                    last_env = env
                    continue
                else:
                    return ret
            
            last_build_env = env
        else:
            if RICH_AVAILABLE:
                console.print(f"[dim]Сборка для {env} уже выполнена, пропускаем...[/dim]")
            else:
                print(f"Сборка для {env} уже выполнена, пропускаем...")
        
        # === Выбор порта ===
        ports = get_serial_ports()
        
        if not ports:
            if RICH_AVAILABLE:
                console.print("[bold red]Не найдено ни одного порта![/bold red]")
                console.print("[yellow]Подключите устройство и нажмите Enter...[/yellow]")
            else:
                print("Не найдено ни одного порта!")
                print("Подключите устройство и нажмите Enter...")
            
            input()
            last_env = env
            continue
        
        if last_port and last_port in ports:
            # Предыдущий порт всё ещё доступен
            if RICH_AVAILABLE:
                console.print()
                use_same_port = Confirm.ask(
                    f"[yellow]Использовать тот же порт ({last_port})?[/yellow]",
                    default=True
                )
            else:
                answer = input(f"Использовать тот же порт ({last_port})? [Y/n]: ").strip().lower()
                use_same_port = answer in ('', 'y', 'yes', 'д', 'да')
            
            if use_same_port:
                port = last_port
            else:
                port = select_port(ports)
        else:
            port = select_port(ports)
        
        if port is None:
            last_env = env
            continue
        
        # === Загрузка ===
        ret = run_upload(env, port, hardware_mc_dir)
        
        if ret == 0:
            # Успех!
            return 0, env, port
        
        # Ошибка загрузки
        if port == last_port and env == last_env:
            # Тот же порт и МК — показываем ошибку
            if RICH_AVAILABLE:
                console.print()
                console.print(Panel(
                    "[bold red]Загрузка не удалась![/bold red]\n\n"
                    "Возможные причины:\n"
                    "• Неправильный порт\n"
                    "• Устройство не в режиме загрузки\n"
                    "• Неправильный выбор МК\n"
                    "• Проблемы с драйверами",
                    border_style="red",
                    title="Ошибка"
                ))
            else:
                print("\n" + "="*50)
                print(" ОШИБКА ЗАГРУЗКИ!")
                print("="*50)
                print(" Возможные причины:")
                print(" • Неправильный порт")
                print(" • Устройство не в режиме загрузки")
                print(" • Неправильный выбор МК")
                print(" • Проблемы с драйверами")
                print("="*50)
        
        # Спрашиваем о повторе
        if RICH_AVAILABLE:
            console.print()
            retry = Confirm.ask("[yellow]Попробовать снова?[/yellow]", default=True)
        else:
            answer = input("Попробовать снова? [Y/n]: ").strip().lower()
            retry = answer in ('', 'y', 'yes', 'д', 'да')
        
        if not retry:
            return ret, env, port
        
        # Сохраняем для следующей итерации
        last_env = env
        last_port = port


def main():
    """Главная функция"""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Интерактивный выбор микроконтроллера для PlatformIO"
    )
    parser.add_argument(
        "command",
        nargs="?",
        choices=["select", "build", "upload", "monitor", "all"],
        default="select",
        help="Команда: select (только выбор), build, upload, monitor, all (build+upload+monitor)"
    )
    parser.add_argument(
        "-e", "--env",
        help="Окружение PlatformIO (пропустить интерактивный выбор)"
    )
    parser.add_argument(
        "-p", "--port",
        help="Порт для загрузки (пропустить интерактивный выбор)"
    )
    
    args = parser.parse_args()
    
    hardware_mc_dir = Path(__file__).parent.parent / "hardware-mc"
    
    # Выполняем команду
    if args.command == "select":
        # Только выбор окружения
        if args.env:
            print(args.env)
        else:
            env = select_environment()
            print(env)
        return 0
    
    elif args.command == "build":
        if args.env:
            env = args.env
        else:
            env = select_environment()
        return run_build(env, hardware_mc_dir)
    
    elif args.command == "upload":
        # Интерактивный цикл с повтором при ошибке
        result = interactive_upload_loop(hardware_mc_dir, args.env, args.port)
        if isinstance(result, tuple):
            return result[0]
        return result
    
    elif args.command == "monitor":
        if args.port:
            port = args.port
        else:
            ports = get_serial_ports()
            port = select_port(ports)
        
        return run_monitor(port, hardware_mc_dir)
    
    elif args.command == "all":
        # Интерактивный цикл загрузки
        result = interactive_upload_loop(hardware_mc_dir, args.env, args.port)
        
        if isinstance(result, tuple):
            ret, env, port = result
            if ret != 0:
                return ret
        else:
            return result
        
        # Монитор после успешной загрузки
        return run_monitor(port, hardware_mc_dir)
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
