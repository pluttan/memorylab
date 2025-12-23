#!/usr/bin/env python3
"""
Утилита для красивого вывода сообщений в Makefile.
Использует rich для форматирования, с ANSI fallback.
"""

import sys

# ANSI цвета для fallback
class Colors:
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    BOLD = '\033[1m'
    DIM = '\033[2m'
    RESET = '\033[0m'

try:
    from rich.console import Console
    from rich.panel import Panel
    console = Console()
    HAS_RICH = True
except ImportError:
    HAS_RICH = False

def _text_width(text: str) -> int:
    """Вычисляет визуальную ширину строки."""
    try:
        import unicodedata
        width = 0
        for char in text:
            # East Asian Wide = 2, остальные = 1
            if unicodedata.east_asian_width(char) in ('W', 'F'):
                width += 2
            else:
                width += 1
        return width
    except Exception:
        return len(text)

def print_header(title: str):
    """Выводит заголовок секции."""
    if HAS_RICH:
        console.print(Panel.fit(title, border_style="cyan"))
    else:
        width = _text_width(title)
        line = "─" * (width + 2)
        padding = " " * (width - len(title) + len(title))  # для выравнивания
        print(f"{Colors.CYAN}╭{line}╮{Colors.RESET}")
        print(f"{Colors.CYAN}│{Colors.RESET} {Colors.BOLD}{title}{Colors.RESET} {Colors.CYAN}│{Colors.RESET}")
        print(f"{Colors.CYAN}╰{line}╯{Colors.RESET}")

def print_success(message: str):
    """Выводит сообщение об успехе."""
    if HAS_RICH:
        console.print(f"[green][[+]][/green] {message}")
    else:
        print(f"{Colors.GREEN}[+]{Colors.RESET} {message}")

def print_info(message: str):
    """Выводит информационное сообщение."""
    if HAS_RICH:
        console.print(f"[cyan][[*]][/cyan] {message}")
    else:
        print(f"{Colors.CYAN}[*]{Colors.RESET} {message}")

def print_warning(message: str):
    """Выводит предупреждение."""
    if HAS_RICH:
        console.print(f"[yellow][[!]][/yellow] {message}")
    else:
        print(f"{Colors.YELLOW}[!]{Colors.RESET} {message}")

def print_error(message: str):
    """Выводит ошибку."""
    if HAS_RICH:
        console.print(f"[red][[-]][/red] {message}")
    else:
        print(f"{Colors.RED}[-]{Colors.RESET} {message}")

def print_done():
    """Выводит сообщение о завершении."""
    if HAS_RICH:
        console.print(Panel.fit("Готово!", border_style="green"))
    else:
        print(f"{Colors.GREEN}╭────────╮{Colors.RESET}")
        print(f"{Colors.GREEN}│{Colors.RESET} {Colors.BOLD}Готово!{Colors.RESET} {Colors.GREEN}│{Colors.RESET}")
        print(f"{Colors.GREEN}╰────────╯{Colors.RESET}")

def print_line():
    """Выводит разделительную линию."""
    print(f"{Colors.DIM}{'─' * 50}{Colors.RESET}")

def main():
    if len(sys.argv) < 2:
        print("Usage: pretty_print.py <command> [message]")
        print("Commands: header, success, info, warning, error, done, line")
        sys.exit(1)
    
    cmd = sys.argv[1]
    msg = " ".join(sys.argv[2:]) if len(sys.argv) > 2 else ""
    
    if cmd == "header":
        print_header(msg)
    elif cmd == "success":
        print_success(msg)
    elif cmd == "info":
        print_info(msg)
    elif cmd == "warning":
        print_warning(msg)
    elif cmd == "error":
        print_error(msg)
    elif cmd == "done":
        print_done()
    elif cmd == "line":
        print_line()
    else:
        print(msg)

if __name__ == "__main__":
    main()
