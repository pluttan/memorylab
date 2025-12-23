"""
IU6 Hardware Memory Lab
========================

Библиотека для проведения экспериментов по исследованию
характеристик динамической памяти.

Кафедра ИУ-6, МГТУ им. Н.Э. Баумана
"""

from .hardware_client import (
    HardwareTesterClient,
    ServerInfo,
    NetworkInterface,
    run_experiment
)

from .generatereport import (
    ReportGenerator,
    generate_report
)

__version__ = "1.0.0"
__author__ = "IU6"

__all__ = [
    "HardwareTesterClient",
    "ServerInfo",
    "NetworkInterface",
    "run_experiment",
    "ReportGenerator",
    "generate_report"
]
