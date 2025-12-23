#!/usr/bin/env python3
"""
Скрипт для автоматической перекомпиляции отчёта при изменении файлов.

Отслеживает:
- report/results.txt - перегенерирует main.typ и компилирует PDF
- report/img/ - компилирует PDF
- report/main.typ и другие .typ файлы - компилирует PDF
"""

import subprocess
import sys
import time
from pathlib import Path
from datetime import datetime

try:
    from watchdog.observers import Observer
    from watchdog.events import FileSystemEventHandler, FileModifiedEvent, FileCreatedEvent
except ImportError:
    print("=" * 50)
    print(" Требуется установить watchdog:")
    print(" pip install watchdog")
    print("=" * 50)
    sys.exit(1)

try:
    from rich.console import Console
    from rich.panel import Panel
    console = Console()
except ImportError:
    # Fallback если rich не установлен
    class FallbackConsole:
        def print(self, *args, **kwargs):
            import re
            msg = str(args[0]) if args else ""
            msg = re.sub(r'\[.*?\]', '', msg)
            print(msg, flush=kwargs.get('flush', False))
    console = FallbackConsole()
    Panel = None


class ReportWatcher(FileSystemEventHandler):
    """Обработчик событий файловой системы для отчёта."""
    
    def __init__(self, project_root: Path, typst_bin: Path):
        self.project_root = project_root
        self.report_dir = project_root / "report"
        self.results_file = self.report_dir / "results.txt"
        self.img_dir = self.report_dir / "img"
        self.main_file = self.report_dir / "main.typ"
        self.typst_bin = typst_bin
        self.last_compile_time = 0
        self.cooldown = 1.0
        
        # Определяем имя PDF из конфига
        self.pdf_file = self._get_pdf_path()
    
    def _get_pdf_path(self) -> Path:
        """Возвращает путь к PDF файлу на основе конфига."""
        try:
            import json
            config_path = self.report_dir / ".report_config.json"
            if config_path.exists():
                with open(config_path, "r", encoding="utf-8") as f:
                    config = json.load(f)
                author_name = config.get("author_name", "Иванов И.И.")
                author_group = config.get("author_group", "ИУ6-42Б")
                return self.project_root / f"Отчет_{author_name}_{author_group}.pdf"
        except Exception:
            pass
        return self.project_root / "Отчет.pdf"
        
    def _should_process(self) -> bool:
        """Проверяет, прошло ли достаточно времени с последней компиляции."""
        now = time.time()
        if now - self.last_compile_time < self.cooldown:
            return False
        self.last_compile_time = now
        return True
    
    def _log(self, message: str, style: str = "cyan"):
        """Выводит сообщение с временной меткой."""
        timestamp = datetime.now().strftime("%H:%M:%S")
        console.print(f"[dim][{timestamp}][/dim] [{style}]{message}[/{style}]", highlight=False)
    
    def _regenerate_and_compile(self):
        """Перегенерирует main.typ и компилирует PDF."""
        if not self._should_process():
            return
            
        self._log("results.txt изменён -> перегенерация main.typ...")
        
        try:
            # Запускаем генерацию без интерактивных промптов
            result = subprocess.run(
                [sys.executable, "-c", 
                 "from iu6hardwarememorylab import generate_report; generate_report()"],
                cwd=self.project_root,
                capture_output=True,
                text=True
            )
            if result.returncode == 0:
                self._log("main.typ обновлён", "green")
                # force=True, чтобы не проверять cooldown повторно
                self._compile_pdf(force=True)
            else:
                self._log(f"Ошибка генерации: {result.stderr}", "red")
        except Exception as e:
            self._log(f"Ошибка: {e}", "red")
    
    def _compile_pdf(self, force: bool = False):
        """Компилирует PDF."""
        if not force and not self._should_process():
            return
        
        # Обновляем путь к PDF (на случай изменения конфига)
        self.pdf_file = self._get_pdf_path()
            
        self._log("Компиляция PDF...")
        
        try:
            result = subprocess.run(
                [str(self.typst_bin), "compile", str(self.main_file), str(self.pdf_file)],
                capture_output=True,
                text=True
            )
            if result.returncode == 0:
                self._log(f"PDF обновлён: {self.pdf_file.name}", "green")
            else:
                self._log(f"Ошибка компиляции:\n{result.stderr}", "red")
        except Exception as e:
            self._log(f"Ошибка: {e}", "red")
    
    def on_modified(self, event):
        """Обработка изменения файла."""
        if event.is_directory:
            return
            
        path = Path(event.src_path)
        
        # Игнорируем временные файлы
        if path.suffix in ('.swp', '.tmp', '~') or path.name.startswith('.'):
            return
        
        # results.txt изменён → перегенерировать и скомпилировать
        if path == self.results_file:
            self._regenerate_and_compile()
            return

        # .report_config.json изменён → перегенерировать и скомпилировать
        if path.name == ".report_config.json":
            self._log("Конфигурация изменена -> перегенерация main.typ...")
            self._regenerate_and_compile()
            return
        
        # Изображение изменено → скомпилировать
        # Проверяем что файл находится в папке img (непосредственно или в подпапках)
        try:
            path.relative_to(self.img_dir)
            if path.suffix.lower() in ('.png', '.jpg', '.jpeg', '.svg'):
                self._log(f"Изображение изменено: {path.name}")
                self._compile_pdf()
                return
        except ValueError:
            pass  # Файл не в img директории
        
        # .typ файл изменён → скомпилировать
        if path.suffix == '.typ':
            try:
                path.relative_to(self.report_dir)
                self._log(f"Typst файл изменён: {path.name}")
                self._compile_pdf()
                return
            except ValueError:
                pass
    
    def on_created(self, event):
        """Обработка создания файла."""
        self.on_modified(event)


def main():
    # Определяем пути
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    report_dir = project_root / "report"
    typst_bin = report_dir / "typst" / "typst"
    
    if not typst_bin.exists():
        console.print("[red][[-]][/red] Typst не найден. Выполните: make report-setup")
        sys.exit(1)
    
    if not report_dir.exists():
        console.print(f"[red][[-]][/red] Папка {report_dir} не найдена")
        sys.exit(1)
    
    if Panel:
        console.print(Panel.fit("Режим наблюдения за отчётом", border_style="cyan"))
    else:
        console.print("[bold]Режим наблюдения за отчётом[/bold]")
    
    console.print(f"[cyan][[*]][/cyan] Папка: {report_dir}")
    console.print("[cyan][[*]][/cyan] Отслеживаемые файлы:")
    console.print("   - results.txt -> перегенерация + компиляция")
    console.print("   - .report_config.json -> перегенерация + компиляция")
    console.print("   - img/*.png -> компиляция")
    console.print("   - *.typ -> компиляция")
    console.print("[dim]Нажмите Ctrl+C для остановки[/dim]")
    
    # Создаём обработчик и наблюдатель
    handler = ReportWatcher(project_root, typst_bin)
    observer = Observer()
    
    # Добавляем директорию для наблюдения (рекурсивно)
    observer.schedule(handler, str(report_dir), recursive=True)
    observer.start()
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        if Panel:
            console.print(Panel.fit("Наблюдение остановлено", border_style="yellow"))
        else:
            console.print("[yellow]Наблюдение остановлено[/yellow]")
        observer.stop()
    
    observer.join()


if __name__ == "__main__":
    main()
