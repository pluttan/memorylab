"""
IU6 Hardware Memory Lab - Report Generator
============================================

Модуль для автоматической генерации отчёта по лабораторной работе
на основе результатов экспериментов.

Кафедра ИУ-6, МГТУ им. Н.Э. Баумана
"""

import os
import re
from pathlib import Path
from typing import Optional, Dict, Any, List
from datetime import datetime
from rich.console import Console
from rich.panel import Panel
from rich import box

# Глобальный экземпляр консоли для красивого вывода
console = Console()


class ReportGenerator:
    """Генератор отчёта по лабораторной работе в формате Typst."""
    
    # Пути по умолчанию относительно корня проекта
    DEFAULT_REPORT_DIR = "report"
    DEFAULT_MAIN_FILE = "main.typ"
    DEFAULT_RESULTS_FILE = "report/results.txt"  # results.txt в папке report
    DEFAULT_IMG_DIR = "report/img"  # img в папке report
    
    # Пути к локальным пакетам
    BMSTU_PKG_PATH = "typst-bmstu/bmstu"
    GOST_PKG_PATH = "typst-g7.32-2017/gost732-2017"
    
    # Размер изображений в отчёте
    IMAGE_WIDTH = 30  # см
    
    # Информация об экспериментах (на основе методических указаний)
    EXPERIMENTS = {
        1: {
            "title": "Исследование расслоения динамической памяти",
            "goal": "Определение способа трансляции физического адреса, используемого при обращении к динамической памяти.",
            "problem": """В связи с конструктивной неоднородностью оперативной памяти, обращение к последовательно 
расположенным данным требует различного времени. Для создания эффективных программ 
необходимо учитывать расслоение памяти, характеризуемое способом трансляции физического адреса.""",
            "method": """Для определения способа трансляции физического адреса применяется процедура замера времени 
обращения к динамической памяти по последовательным адресам с изменяющимся шагом чтения. 
Результат представляется зависимостью времени, потраченного на чтение ячеек, от шага чтения.""",
            "analysis": """По графику определяются следующие параметры:
- *Точка T1* — первый локальный экстремум, соответствует минимальному шагу чтения при постоянном обращении к одному банку
- *Точка T2* — глобальный экстремум, наихудший шаг при обращении к памяти (постоянное открытие/закрытие страниц)
- Количество банков: Б = T1/П, где П — размер линейки кэш-памяти
- Размер страницы: РС = T2/Б""",
            "img_name": "exp1_memory_stratification",
            "param_labels": {
                "param1_kb": "Максимальное расстояние",
                "param2_b": "Шаг увеличения расстояния",
                "param3_mb": "Размер массива",
            },
            "result_labels": {
                "T1_step_bytes": "Точка T1 (первый максимум)",
                "T1_time_us": "Время в точке T1",
                "T2_step_bytes": "Точка T2 (глобальный максимум)", 
                "T2_time_us": "Время в точке T2",
                "estimated_banks": "Количество банков памяти",
                "estimated_page_size_bytes": "Размер страницы памяти",
            }
        },
        2: {
            "title": "Сравнение эффективности ссылочных и векторных структур",
            "goal": "Оценка влияния зависимости команд по данным на эффективность вычислений.",
            "problem": """Обработка зависимых данных происходит когда результат работы одной команды используется 
в качестве адреса операнда другой. При программировании такими операндами являются указатели, 
используемые при обработке ссылочных структур: списков, деревьев, графов. 

Обработка данных структур процессорами с длинными конвейерами команд приводит к увеличению 
времени работы: адрес загружаемого операнда становится известным только после обработки 
предыдущей команды. Обработка векторных структур (массивов) позволяет эффективнее 
использовать аппаратные возможности ЭВМ.""",
            "method": """Для сравнения эффективности применяется профилировка двух алгоритмов поиска минимального 
значения: первый использует список, второй — массив. Время работы алгоритма поиска в списке 
зависит от его фрагментации (среднего расстояния между элементами).""",
            "analysis": """Красный график показывает время работы алгоритма со списком, зеленый — с массивом.
Отношение времени обработки списка к массиву показывает степень неэффективности ссылочных структур.""",
            "img_name": "exp2_list_vs_array",
            "param_labels": {
                "param1_m": "Количество элементов",
                "param2_kb": "Максимальная фрагментация",
                "param3_kb": "Шаг фрагментации",
            },
            "result_labels": {
                "total_list_time_us": "Общее время работы со списком",
                "total_array_time_us": "Общее время работы с массивом",
                "list_to_array_ratio": "Отношение времени (список/массив)",
            }
        },
        3: {
            "title": "Исследование эффективности программной предвыборки",
            "goal": "Выявление способов ускорения вычислений благодаря применению предвыборки данных.",
            "problem": """Обработка больших массивов информации связана с открытием большого количества физических 
страниц памяти. При первом обращении к странице наблюдается увеличенное время доступа из-за:
- Преобразования логического адреса в физический (через TLB буфер)
- Открытия страницы динамической памяти
- Сохранения данных в кэш-памяти

Первое обращение при отсутствии информации в TLB вызывает двойное обращение к ОП: 
сначала за информацией из таблицы страниц, затем за данными.""",
            "method": """Эксперимент сравнивает два варианта последовательного чтения страниц:
1) Без оптимизации — приводит к дополнительным двойным обращениям
2) С предвыборкой — перед чтением выполняется загрузка информации в TLB данных""",
            "analysis": """Красный график (с пиками) — время без предвыборки. Зеленый график (без пиков) — 
с предвыборкой. Пики соответствуют первым обращениям к новым страницам памяти.""",
            "img_name": "exp3_prefetch",
            "param_labels": {
                "param1_b": "Шаг расстояния",
                "param2_kb": "Размер массива",
            },
            "result_labels": {
                "total_no_prefetch_ns": "Общее время без предвыборки",
                "total_prefetch_ns": "Общее время с предвыборкой",
                "no_prefetch_to_prefetch_ratio": "Отношение времени",
            }
        },
        4: {
            "title": "Исследование способов эффективного чтения оперативной памяти",
            "goal": "Исследование возможности ускорения вычислений благодаря использованию структур данных, оптимизирующих механизм чтения памяти.",
            "problem": """При обработке информации из нескольких страниц и банков ОП возникают задержки, связанные 
с открытием и закрытием страниц DRAM. При интенсивной обработке нескольких массивов 
аппаратная предвыборка часто не может организовать эффективную загрузку данных. 

Объёмы запрошенных данных оказываются меньше размера передаваемого пакета, что приводит 
к неполному использованию пропускной способности памяти.""",
            "method": """Сравниваются два алгоритма обработки нескольких блоков памяти:
1) Неоптимизированный — несколько отдельных массивов в памяти
2) Оптимизированный — чередующиеся данные каждого массива в непрерывной области""",
            "analysis": """Красный график — неоптимизированная структура, зелёный — оптимизированная.
Оптимизация снижает количество кэш-промахов, сокращает открытия/закрытия страниц DRAM.""",
            "img_name": "exp4_memory_read_optimization",
            "param_labels": {
                "param1_mb": "Размер массива",
                "param2_streams": "Количество потоков данных",
            },
            "result_labels": {
                "total_separate_time_us": "Время с отдельными массивами",
                "total_optimized_time_us": "Время с оптимизированным массивом",
                "separate_to_optimized_ratio": "Отношение времени",
            }
        },
        5: {
            "title": "Исследование конфликтов в кэш-памяти",
            "goal": "Исследование влияния конфликтов кэш-памяти на эффективность вычислений.",
            "problem": """Наборно-ассоциативная кэш-память состоит из линеек, организованных в независимые банки. 
Выбор банка выполняется ассоциативно, а целевая линейка определяется по младшей части 
физического адреса.

Попытка читать данные с шагом, кратным размеру банка, приводит к их помещению в один набор. 
Если количество запросов превышает степень ассоциативности, происходит постоянное вытеснение 
данных из кэша, при этом большая часть кэша остаётся незадействованной.""",
            "method": """Сравниваются две процедуры чтения данных:
1) С конфликтами — шаг чтения кратен размеру банка
2) Без конфликтов — добавляется смещение на размер линейки для выбора другого набора""",
            "analysis": """Красный график — чтение с конфликтами, зелёный — без конфликтов.
Разница показывает влияние конфликтов кэша на производительность.""",
            "img_name": "exp5_cache_conflicts",
            "param_labels": {
                "param1_kb": "Размер банка кэш-памяти",
                "param2_b": "Размер линейки кэш-памяти",
                "param3_lines": "Количество линеек",
            },
            "result_labels": {
                "total_conflict_time_us": "Общее время с конфликтами",
                "total_no_conflict_time_us": "Общее время без конфликтов",
                "conflict_to_no_conflict_ratio": "Отношение времени",
            }
        },
        6: {
            "title": "Сравнение алгоритмов сортировки",
            "goal": "Исследование способов эффективного использования памяти и выявление наиболее эффективных алгоритмов сортировки.",
            "problem": """Алгоритм QuickSort имеет сложность O(n·log(n)) в среднем. Алгоритмы без парных сравнений 
(Counting Sort, Radix Sort) могут иметь меньшую сложность.

Radix-Counting алгоритм имеет сложность O(n/log(n)), т.е. меньше линейной. Для многопроцессорных 
систем используется оптимизированная стратегия, учитывающая пакетный режим обмена с памятью.""",
            "method": """Сравниваются три алгоритма:
1) QuickSort — классический алгоритм с парными сравнениями
2) Radix-Counting Sort — поразрядная сортировка без сравнений
3) Radix-Counting Sort (оптим.) — версия для 8-процессорной системы""",
            "analysis": """Фиолетовый график — QuickSort, красный — Radix-Counting, зелёный — оптимизированный Radix-Counting.
Radix-Counting показывает лучшие результаты на больших массивах.""",
            "img_name": "exp6_sorting_algorithms",
            "param_labels": {
                "param1_m": "Количество элементов",
                "param2_k": "Шаг увеличения размера",
            },
            "result_labels": {
                "total_quicksort_us": "Общее время QuickSort",
                "total_radix_us": "Общее время Radix-Counting",
                "total_radix_opt_us": "Общее время Radix-Counting (оптим.)",
                "quicksort_to_radix_ratio": "QuickSort/Radix-Counting",
                "quicksort_to_radix_opt_ratio": "QuickSort/Radix-Counting (оптим.)",
            }
        },
    }
    
    def __init__(
        self,
        report_dir: Optional[str] = None,
        results_file: Optional[str] = None,
        img_dir: Optional[str] = None,
        project_root: Optional[str] = None,
        title_config: Optional[Dict[str, str]] = None
    ):
        """
        Инициализация генератора отчёта.
        
        Args:
            report_dir: Путь к папке отчёта
            results_file: Путь к файлу с результатами экспериментов
            img_dir: Путь к папке с изображениями
            project_root: Корневая директория проекта
            title_config: Конфигурация титульной страницы
        """
        # Определяем корень проекта
        if project_root:
            self.project_root = Path(project_root)
        else:
            # Предполагаем, что скрипт лежит в iu6hardwarememorylab
            self.project_root = Path(__file__).parent.parent
        
        self.report_dir = Path(report_dir) if report_dir else self.project_root / self.DEFAULT_REPORT_DIR
        self.results_file = Path(results_file) if results_file else self.project_root / self.DEFAULT_RESULTS_FILE
        self.img_dir = Path(img_dir) if img_dir else self.project_root / self.DEFAULT_IMG_DIR
        self.main_file = self.report_dir / self.DEFAULT_MAIN_FILE
        
        # Конфигурация титульной страницы
        self.title_config = title_config or {}
        
        # Кэш для результатов экспериментов
        self._parsed_results: Dict[int, Dict[str, Any]] = {}
        
    def _check_packages(self) -> bool:
        """Проверяет наличие необходимых пакетов Typst."""
        bmstu_path = self.report_dir / self.BMSTU_PKG_PATH
        gost_path = self.report_dir / self.GOST_PKG_PATH
        
        if not bmstu_path.exists():
            console.print(f"[red][[-]][/red] Пакет bmstu не найден: {bmstu_path}")
            console.print("    Выполните: make report-setup")
            return False
        
        if not gost_path.exists():
            console.print(f"[red][[-]][/red] Пакет gost732-2017 не найден: {gost_path}")
            console.print("    Выполните: make report-setup")
            return False
        
        return True
    
    def _get_images(self) -> List[Path]:
        """Получает список изображений из папки img."""
        images = []
        if self.img_dir.exists():
            for ext in ['*.png', '*.jpg', '*.jpeg', '*.svg']:
                images.extend(self.img_dir.glob(ext))
        return sorted(images)
    
    def _parse_results(self) -> Dict[int, Dict[str, Any]]:
        """
        Парсит файл results.txt и извлекает данные экспериментов.
        
        Returns:
            Словарь {номер_эксперимента: {params: {...}, results: {...}}}
        """
        if self._parsed_results:
            return self._parsed_results
        
        if not self.results_file.exists():
            console.print(f"[yellow][[!]][/yellow] Файл результатов не найден: {self.results_file}")
            return {}
        
        with open(self.results_file, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Разбиваем на блоки по разделителю
        blocks = content.split("=" * 70)
        
        current_exp = None
        current_section = None
        
        for block in blocks:
            block = block.strip()
            if not block or "РЕЗУЛЬТАТЫ ЭКСПЕРИМЕНТОВ" in block:
                continue
            
            lines = block.split('\n')
            
            for line in lines:
                line = line.strip()
                
                # Ищем заголовок эксперимента
                exp_match = re.match(r'Эксперимент\s+(\d+):', line)
                if exp_match:
                    current_exp = int(exp_match.group(1))
                    self._parsed_results[current_exp] = {"params": {}, "results": {}}
                    continue
                
                # Определяем секцию
                if line == "Параметры:":
                    current_section = "params"
                    continue
                elif line == "Результаты:":
                    current_section = "results"
                    continue
                
                # Парсим ключ-значение
                if current_exp and current_section and ': ' in line:
                    key, value = line.split(': ', 1)
                    key = key.strip()
                    value = value.strip()
                    
                    # Пробуем преобразовать в число
                    try:
                        if '.' in value:
                            value = float(value)
                        else:
                            value = int(value)
                    except ValueError:
                        pass
                    
                    self._parsed_results[current_exp][current_section][key] = value
        
        return self._parsed_results
    
    def _format_value(self, key: str, value: Any) -> str:
        """Форматирует значение для отображения."""
        # Сначала проверяем суффикс ключа для определения единиц измерения
        if "_us" in key:
            if isinstance(value, (int, float)):
                if value >= 1000000:
                    return f"{value/1000000:.2f} с"
                elif value >= 1000:
                    return f"{value/1000:.2f} мс"
                else:
                    return f"{value:.2f} мкс"
        elif "_ns" in key:
            if isinstance(value, (int, float)):
                if value >= 1000000:
                    return f"{value/1000000:.2f} мс"
                elif value >= 1000:
                    return f"{value/1000:.2f} мкс"
                else:
                    return f"{value:.2f} нс"
        elif "_bytes" in key:
            if isinstance(value, (int, float)):
                if value >= 1024*1024:
                    return f"{value/(1024*1024):.2f} МБ"
                elif value >= 1024:
                    return f"{value/1024:.2f} КБ"
                else:
                    return f"{int(value)} байт"
        elif "_ratio" in key:
            if isinstance(value, (int, float)):
                return f"{value:.2f}x"
        elif "_kb" in key:
            if isinstance(value, (int, float)):
                return f"{value} КБ"
        elif "_mb" in key:
            if isinstance(value, (int, float)):
                return f"{value} МБ"
        elif "_m" in key and "_mb" not in key:
            if isinstance(value, (int, float)):
                return f"{value} М"
        elif "_k" in key and "_kb" not in key:
            if isinstance(value, (int, float)):
                return f"{value} К"
        elif "_b" in key and "_bytes" not in key and "_mb" not in key and "_kb" not in key:
            if isinstance(value, (int, float)):
                return f"{value} байт"
        elif "_lines" in key or "_streams" in key:
            return str(int(value))
        elif "banks" in key:
            return str(int(value))
        
        # Общее форматирование для необработанных значений
        if isinstance(value, float):
            return f"{value:.4f}"
        return str(value)
    
    def _generate_header(self) -> str:
        """Генерирует заголовок документа с импортами."""
        return '''// Отчёт по лабораторной работе 4
// Исследование характеристик динамической памяти
// 
// Сгенерировано автоматически: {date}
// Кафедра ИУ-6, МГТУ им. Н.Э. Баумана

// Импорт локальных пакетов
#import "{bmstu_path}/bmstu.typ": *
#import "{gost_path}/g7.32-2017.typ": *

// Применяем стиль ГОСТ 7.32-2017
#show: гост732-2017
'''.format(
            date=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            bmstu_path=self.BMSTU_PKG_PATH,
            gost_path=self.GOST_PKG_PATH
        )
    
    def _generate_title_page(self) -> str:
        """Генерирует титульную страницу."""
        # Получаем данные из конфигурации или используем значения по умолчанию
        study_field = self.title_config.get("study_field", "09.03.01")
        
        # Определяем направление на основе кода
        if study_field == "09.03.01":
            direction = "Инфроматика и вычислительная техника"
        else:  # 09.03.03
            direction = "Прикладная информатика"
        
        author_name = self.title_config.get("author_name", "И.И. Иванов")
        author_group = self.title_config.get("author_group", "ИУ6-72Б")
        adviser_name = self.title_config.get("adviser_name", "А.Ю. Попов")
        
        return f'''
// ==================== ТИТУЛЬНАЯ СТРАНИЦА ====================
#титул_отчета(
    факультет: "Информатика и системы управления",
    кафедра: "Компьютерные системы и сети",
    код_направления: "{study_field}",
    направление: "{direction}",
    тип_работы: "лабораторной работе",
    номер_работы: "4",
    дисциплина: "Организация ЭВМ и систем",
    название_работы: "Организация памяти конвейерных",
    название_работы_кол2: "суперскалярных электронных вычислительных машин",
    автор: (фио: "{author_name}", группа: "{author_group}"),
    руководитель: (фио: "{adviser_name}"),
    город: "Москва")
'''
    
    def _generate_toc(self) -> str:
        """Генерирует содержание."""
        return '''
// ==================== СОДЕРЖАНИЕ ====================
#содержание()

'''
    
    def _generate_introduction(self) -> str:
        """Генерирует введение."""
        return '''
// ==================== ВВЕДЕНИЕ ====================
= Введение

Целью данной лабораторной работы является исследование характеристик динамической 
оперативной памяти (DRAM) и особенностей работы кэш-памяти процессора.

В рамках работы проводятся следующие эксперименты:

+ Исследование расслоения динамической памяти;
+ Сравнение эффективности ссылочных и векторных структур данных;
+ Исследование эффективности программной предвыборки;
+ Оптимизация чтения оперативной памяти;
+ Исследование конфликтов в кэш-памяти;
+ Сравнение алгоритмов сортировки.

'''
    
    def _generate_experiment_section(self, num: int) -> str:
        """Генерирует секцию эксперимента с данными из results.txt."""
        exp_info = self.EXPERIMENTS.get(num, {})
        title = exp_info.get("title", f"Эксперимент {num}")
        goal = exp_info.get("goal", "")
        problem = exp_info.get("problem", "")
        method = exp_info.get("method", "")
        analysis = exp_info.get("analysis", "")
        img_name = exp_info.get("img_name", f"exp{num}")
        param_labels = exp_info.get("param_labels", {})
        result_labels = exp_info.get("result_labels", {})
        
        results = self._parse_results()
        exp_data = results.get(num, {"params": {}, "results": {}})
        
        section = f'''
// ==================== ЭКСПЕРИМЕНТ {num} ====================
= Эксперимент {num}: {title}

== Цель эксперимента

{goal}

== Описание проблемы

{problem}

== Суть эксперимента

{method}

'''
        # Добавляем параметры эксперимента
        if exp_data["params"]:
            section += '''== Параметры эксперимента

'''
            section += '''#таблица(table(
  columns: (1fr, 1fr),
'''
            for key, value in exp_data["params"].items():
                label = param_labels.get(key, key)
                formatted_value = self._format_value(key, value)
                section += f'  [ {label} ], [ {formatted_value} ],\n'
            section += f'))[ Параметры эксперимента {num} ]\n\n'
        
        # Добавляем изображение если есть
        img_path = self.img_dir / f"{img_name}.png"
        rel_img_path = f"img/{img_name}.png"
        
        section += '''== Результаты эксперимента

'''
        
        if img_path.exists():
            section += f'''#рис(image("{rel_img_path}", width: размер({self.IMAGE_WIDTH})))[{title}] <exp{num}>

'''
        else:
            section += f'''// Изображение не найдено: {rel_img_path}
// После проведения эксперимента раскомментируйте строку ниже:
// #рис(image("{rel_img_path}", width: размер({self.IMAGE_WIDTH})))[{title}] <exp{num}>

'''
        
        # Добавляем анализ графика
        if analysis:
            section += f'''{analysis}

'''
        
        # Добавляем таблицу результатов
        if exp_data["results"]:
            section += '''#таблица(table(
  columns: (1.5fr, 1fr),
'''
            for key, value in exp_data["results"].items():
                label = result_labels.get(key, key)
                formatted_value = self._format_value(key, value)
                section += f'  [ {label} ], [ {formatted_value} ],\n'
            section += f'))[ Численные результаты эксперимента {num} ]\n\n'
        else:
            section += '''// TODO: Результаты будут добавлены после проведения эксперимента

'''
        
        return section
    
    def _generate_experiments(self) -> str:
        """Генерирует секции всех экспериментов."""
        content = ""
        for num in range(1, 7):
            content += self._generate_experiment_section(num)
        return content
    
    def _generate_conclusion(self) -> str:
        """Генерирует заключение."""
        results = self._parse_results()
        
        conclusion = '''
// ==================== ЗАКЛЮЧЕНИЕ ====================
= Заключение

В ходе выполнения лабораторной работы были исследованы характеристики динамической 
оперативной памяти и особенности работы кэш-памяти процессора.

'''
        # Добавляем краткую сводку результатов
        if results:
            for num in sorted(results.keys()):
                exp_info = self.EXPERIMENTS.get(num, {})
                title = exp_info.get("title", f"Эксперимент {num}")
                exp_results = results[num].get("results", {})
                
                if exp_results:
                    conclusion += f'*{title}*\n\n'
                    # Выбираем ключевой результат
                    for key, value in list(exp_results.items())[:2]:
                        label = exp_info.get("result_labels", {}).get(key, key)
                        formatted_value = self._format_value(key, value)
                        conclusion += f'- {label}: {formatted_value}\n'
                    conclusion += '\n'
        
        conclusion += '''
// TODO: Добавить общие выводы и рекомендации

'''
        return conclusion
    
    def generate(self) -> bool:
        """
        Генерирует файл отчёта main.typ.
        
        Returns:
            True если генерация успешна
        """
        console.print(Panel.fit("Генерация отчёта", border_style="cyan"))
        
        # Проверяем наличие пакетов
        if not self._check_packages():
            return False
        
        # Парсим результаты
        results = self._parse_results()
        console.print(f"[cyan][[*]][/cyan] Найдено экспериментов в results.txt: {len(results)}")
        
        # Собираем контент
        content = ""
        content += self._generate_header()
        content += self._generate_title_page()
        content += self._generate_toc()
        content += self._generate_introduction()
        content += self._generate_experiments()
        content += self._generate_conclusion()
        
        # Записываем файл
        self.report_dir.mkdir(parents=True, exist_ok=True)
        
        with open(self.main_file, 'w', encoding='utf-8') as f:
            f.write(content)
        
        images = self._get_images()
        console.print(f"[green][[+]][/green] Файл: {self.main_file}")
        console.print(f"[cyan][[*]][/cyan] Изображений найдено: {len(images)}")
        if images:
            for img in images:
                console.print(f"   - {img.name}")
        console.print(Panel.fit("Готово!", border_style="green"))
        
        return True


def prompt_title_config() -> Dict[str, str]:
    """
    Интерактивно запрашивает данные для титульной страницы.
    
    Returns:
        Словарь с конфигурацией титульной страницы
    """
    console.print(Panel.fit("Настройка титульной страницы", border_style="cyan"))
    
    # Направление подготовки
    console.print("\n[bold]Выберите направление подготовки:[/bold]")
    console.print("  1) 09.03.01 - ИВТ (Вычислительные машины, комплексы, системы и сети)")
    console.print("  2) 09.03.03 - ПИ (Прикладная информатика)")
    
    while True:
        choice = input("\nВаш выбор (1 или 2) [1]: ").strip() or "1"
        if choice in ("1", "2"):
            break
        console.print("  [yellow][[!]][/yellow] Введите 1 или 2")
    
    study_field = "09.03.01" if choice == "1" else "09.03.03"
    
    # Данные автора
    console.print("\n[bold]--- Данные студента ---[/bold]")
    author_name = input("ФИО студента (И.О. Фамилия): ").strip()
    if not author_name:
        author_name = "И.И. Иванов"
        console.print(f"  [dim]Используется: {author_name}[/dim]")
    
    author_group = input("Группа: ").strip()
    if not author_group:
        author_group = "ИУ6-72Б"
        console.print(f"  [dim]Используется: {author_group}[/dim]")
    
    # Данные руководителя
    console.print("\n[bold]--- Данные руководителя ---[/bold]")
    adviser_name = input("ФИО руководителя (И.О. Фамилия) [А.Ю. Попов]: ").strip()
    if not adviser_name:
        adviser_name = "А.Ю. Попов"
    
    # === Логика переименования папки проекта ===
    new_project_root = None
    
    try:
        # Извлекаем фамилию (последнее слово, чтобы корректно обработать "А.П. Плютто")
        parts = author_name.split()
        surname = parts[-1] if parts else "Unknown"
        
        # Простая транслитерация
        translit_map = {
            'а': 'a', 'б': 'b', 'в': 'v', 'г': 'g', 'д': 'd', 'е': 'e', 'ё': 'yo',
            'ж': 'zh', 'з': 'z', 'и': 'i', 'й': 'y', 'к': 'k', 'л': 'l', 'м': 'm',
            'н': 'n', 'о': 'o', 'п': 'p', 'р': 'r', 'с': 's', 'т': 't', 'у': 'u',
            'ф': 'f', 'х': 'kh', 'ц': 'ts', 'ч': 'ch', 'ш': 'sh', 'щ': 'shch',
            'ъ': '', 'ы': 'y', 'ь': '', 'э': 'e', 'ю': 'yu', 'я': 'ya'
        }
        
        transliterated = ""
        for char in surname.lower():
            transliterated += translit_map.get(char, char)
            
        # Делаем первую букву заглавной
        if transliterated:
            transliterated = transliterated.capitalize()
            
            new_dir_name = f"lab4_{transliterated}"
            current_dir = Path.cwd()
            
            if current_dir.name != new_dir_name:
                console.print(Panel.fit(f"Переименование проекта: {current_dir.name} -> {new_dir_name}", border_style="cyan"))
                
                new_path = current_dir.parent / new_dir_name
                if not new_path.exists():
                    current_dir.rename(new_path)
                    console.print(f"[green][[+]][/green] Проект переименован: {current_dir.name} -> {new_dir_name}")
                    # Меняем рабочую директорию процесса
                    import os
                    os.chdir(new_path)
                    new_project_root = new_path
                else:
                    console.print(f"[yellow][[!]][/yellow] Папка {new_dir_name} уже существует.")
                    # Если папка существует, возможно мы хотим использовать её как root?
                    # Но пока просто предупреждаем.
    except Exception as e:
        console.print(f"[yellow][[!]][/yellow] Ошибка переименования: {e}")

    console.print(Panel.fit("Конфигурация сохранена!", border_style="green"))
    
    config = {
        "study_field": study_field,
        "author_name": author_name,
        "author_group": author_group,
        "adviser_name": adviser_name
    }
    
    # Сохраняем в файл для последующего использования
    try:
        import json
        report_dir = Path("report")
        if not report_dir.exists():
            report_dir.mkdir(parents=True, exist_ok=True)
            
        config_path = report_dir / ".report_config.json"
        with open(config_path, "w", encoding="utf-8") as f:
            json.dump(config, f, ensure_ascii=False, indent=2)
    except Exception as e:
        console.print(f"[yellow][[!]][/yellow] Не удалось сохранить конфигурацию в файл: {e}")
        
    return config, new_project_root


def load_title_config() -> Optional[Dict[str, str]]:
    """Загружает конфигурацию из файла."""
    try:
        import json
        config_path = Path("report/.report_config.json")
        if config_path.exists():
            with open(config_path, "r", encoding="utf-8") as f:
                return json.load(f)
    except Exception:
        pass
    return None


def generate_report(
    project_root: Optional[str] = None,
    report_dir: Optional[str] = None,
    results_file: Optional[str] = None,
    img_dir: Optional[str] = None,
    title_config: Optional[Dict[str, str]] = None,
    interactive: bool = False,
    perform_build: bool = False
) -> bool:
    """
    Удобная функция для генерации отчёта.
    ...
    """
    current_root = project_root
    
    # Если интерактивный режим - спрашиваем и сохраняем
    if interactive and title_config is None:
        title_config, new_root = prompt_title_config()
        if new_root:
            current_root = str(new_root)
    
    # Если конфиг не передан, пробуем загрузить из файла
    if title_config is None:
        title_config = load_title_config()
    
    generator = ReportGenerator(
        project_root=current_root,
        report_dir=report_dir,
        results_file=results_file,
        img_dir=img_dir,
        title_config=title_config
    )
    
    success = generator.generate()
    
    if success and perform_build:
        console.print("\n[cyan][[*]][/cyan] Запуск сборки отчёта...")
        import subprocess
        import shutil
        
        try:
            # Запускаем finish-цель из Makefile в текущей (возможно обновленной) директории
            subprocess.run(["make", "report-finish"], check=True)
            
            # После успешной сборки копируем PDF в корень проекта с нужным именем
            if title_config:
                author_name = title_config.get("author_name", "Иванов И.И.")
                author_group = title_config.get("author_group", "ИУ6-42Б")
                
                # Формируем имя файла: Отчет_И.О. Фамилия_группа.pdf
                pdf_filename = f"Отчет_{author_name}_{author_group}.pdf"
                
                source_pdf = Path("report/main.pdf")
                target_pdf = Path(pdf_filename)
                
                if source_pdf.exists():
                    shutil.copy2(source_pdf, target_pdf)
                    console.print(f"\n[green][[+]][/green] PDF скопирован: {target_pdf}")
                    
        except subprocess.CalledProcessError as e:
            console.print(f"[red][[-]][/red] Ошибка сборки: {e}")
            return False
        except KeyboardInterrupt:
            # Обработка Ctrl+C, так как watchdog будет здесь
            return True
            
    return success


if __name__ == "__main__":
    import sys
    
    # Если запускаем напрямую, определяем корень проекта
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    
    # Проверяем аргументы
    interactive = "--interactive" in sys.argv or "-i" in sys.argv
    perform_build = "perform_build=True" in sys.argv or "--build" in sys.argv
    
    # Если perform_build передан как именованный аргумент в строке вызова python -c
    # (как мы делаем в Makefile), он будет в globals() если вызывать функцию напрямую,
    # но здесь мы парсим sys.argv для __main__
    
    success = generate_report(
        project_root=str(project_root), 
        interactive=interactive,
        perform_build=perform_build
    )
    sys.exit(0 if success else 1)

