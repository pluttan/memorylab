"""Film 8: DOOM column drawing in two modes. Branch mode reloads the colour map pointer and
checks the loop on every pixel; JIT mode keeps the pointer in a register and draws 8 pixels
per check. The modes switch every second, the server reads the frame times."""
import sys
from pathlib import Path

from manim import LEFT, AnimationGroup, Create, FadeIn, FadeOut, Indicate, LaggedStart, VGroup

sys.path.insert(0, str(Path(__file__).parent))
from common import (DIM, GREEN, RED, SUB, SURF, TEXT, Film, Plot, blink, cell, code, hide, panel, roll, show,  # noqa: E402
                    txt)
from model import jit_frames  # noqa: E402

SX0, SY0, SC = -6.5, 2.6, 0.27      # screen: 12 x 8 pixels from the top-left corner
COLS, ROWS, COL = 12, 8, 5
WALL = ['#fab387', '#f9e2af', '#fab387', '#eba0ac', '#fab387', '#f9e2af', '#eba0ac', '#fab387']
MEM_X, MEM_Y, REG_Y = -1.85, 2.2, 1.0


class Exp8Jit(Film):
    TITLE = 'Опыт 8. Самомодифицирующийся код'

    def story(self):
        self.draw_screen()
        self.branch_mode()
        self.jit_mode()
        self.frames()

    def pix(self, c, r):
        return SX0 + (c + 0.5) * SC, SY0 - (r + 0.5) * SC

    def draw_screen(self):
        self.pixels = [[cell(*self.pix(c, r), SC - 0.02, SC - 0.02) for c in range(COLS)] for r in range(ROWS)]
        screen = VGroup(*[p for row in self.pixels for p in row])
        self.mem = panel(MEM_X, MEM_Y, 2.2, 0.55, 'память')
        self.mem.add(txt('dc_colormap', 14, TEXT).move_to((MEM_X, MEM_Y, 0)))
        self.reg = panel(MEM_X, REG_Y, 2.2, 0.55, 'регистр')
        self.reg_text = txt('пусто', 14, DIM).move_to((MEM_X, REG_Y, 0))
        self.loads, self.checks = 0, 0
        self.counter = self.counters()
        # Pixels come in along the diagonals of the screen
        order = sorted(screen, key=lambda m: m.get_center()[0] - m.get_center()[1])
        appear = LaggedStart(LaggedStart(*[FadeIn(m, scale=0.5) for m in order], lag_ratio=0.01),
                             show(self.mem, lag=0.2), show(self.reg, self.reg_text, lag=0.2),
                             show(self.counter, lag=0.2), lag_ratio=0.25)
        self.say('Опыт меряет время кадра игры DOOM в двух режимах рисования.',
                 'Игра пишет время каждого кадра в файл jit_benchmark.csv.', extra=[appear], run_time=2.4)
        self.say('Стены DOOM рисует вертикальными столбцами. Функция R_DrawColumn рисует один столбец.',
                 'Цвет пикселя берётся из таблицы освещения dc_colormap.')

    def counters(self):
        return VGroup(txt(f'чтений указателя: {self.loads}', 15, SUB),
                      txt(f'проверок условия: {self.checks}', 15, SUB)).arrange(
            (0, -1, 0), aligned_edge=LEFT, buff=0.12).move_to((SX0, 0.05, 0), aligned_edge=LEFT)

    def bump(self, loads, checks):
        """Roll only the counters that change."""
        old = (self.loads, self.checks)
        self.loads += loads
        self.checks += checks
        new = self.counters()
        anims = []
        for k, (was, now) in enumerate(zip(old, (self.loads, self.checks))):
            if was != now:
                anims += roll(self.counter[k], new[k], dy=0)
                self.counter.submobjects[k] = new[k]
        return anims

    def load(self):
        """A copy of the pointer flies from memory into the register; returns the animation."""
        flyer = self.mem[2].copy().set_color(RED)
        new = txt('dc_colormap', 14, TEXT).move_to((MEM_X, REG_Y, 0))
        old, self.reg_text = self.reg_text, new
        return AnimationGroup(blink(self.mem[0], RED, 0.35),
                              LaggedStart(flyer.animate(path_arc=-0.8).move_to(new).set_opacity(0),
                                          AnimationGroup(FadeOut(old), FadeIn(new, scale=1.3)), lag_ratio=0.6))

    def clear_column(self, col):
        self.play(LaggedStart(*[self.pixels[r][col].animate.set_fill(SURF, 1) for r in range(ROWS)],
                              lag_ratio=0.1), run_time=0.6)

    def branch_mode(self):
        self.prog = code(['do {', '    colormap = dc_colormap;      // volatile',
                          '    *dest = colormap[source[frac >> 16]];', '    dest += SCREENWIDTH;',
                          '} while (count--);'], -6.5, -0.85, size=14, dy=0.28)
        self.play(show(self.prog, lag=0.12), run_time=0.8)
        self.say('Режим Branch: указатель volatile, процессор читает его из памяти на каждый пиксель.',
                 'После каждого пикселя цикл проверяет условие.', hold=0)
        for r in range(ROWS):
            rt = 1.1 if r < 2 else 0.6
            pixel = self.pixels[r][COL].animate.set_fill(WALL[r], 0.9)
            check = Indicate(self.prog[4], color=RED, scale_factor=1.05)
            self.play(LaggedStart(self.load(), AnimationGroup(pixel, check, *self.bump(1, 1)), lag_ratio=0.55),
                      run_time=rt)
        self.say('Столбец из 8 пикселей: 8 чтений указателя и 8 проверок условия.')
        self.play(hide(self.prog), run_time=0.5)
        self.clear_column(COL)

    def jit_mode(self):
        self.prog = code(['colormap = dc_colormap;          // в регистре', 'while (count >= 7) {',
                          '    *dest = colormap[...]; dest += SCREENWIDTH;', '    ... ещё 7 пикселей ...',
                          '    count -= 8;', '}'], -6.5, -0.85, size=14, dy=0.28)
        self.loads, self.checks = 0, 0
        new = VGroup(txt('чтений указателя: 0', 15, SUB), txt('проверок условия: 0', 15, SUB)).arrange(
            (0, -1, 0), aligned_edge=LEFT, buff=0.12).move_to((SX0, 0.05, 0), aligned_edge=LEFT)
        self.play(show(self.prog, lag=0.12), *roll(self.counter[0], new[0], dy=0), *roll(self.counter[1], new[1], dy=0),
                  run_time=0.8)
        self.counter = new
        self.say('Режим JIT: указатель читается один раз и лежит в регистре.',
                 'Цикл развёрнут: один проход рисует 8 пикселей.', hold=0)
        self.play(self.load(), run_time=1.1)
        self.play(LaggedStart(*[self.pixels[r][COL].animate.set_fill(WALL[r], 0.9) for r in range(ROWS)],
                              lag_ratio=0.08), Indicate(self.prog[1], color=GREEN, scale_factor=1.05),
                  *self.bump(1, 1), run_time=0.9)
        self.wait(1.5)
        self.say('Тот же столбец: одно чтение указателя и одна проверка условия.',
                 'Процессору реже приходится угадывать переход и ждать память.')
        self.play(hide(self.prog), run_time=0.5)

    def frames(self):
        data = jit_frames()
        self.play(hide(VGroup(*[p for row in self.pixels for p in row]), self.mem, self.reg, self.reg_text,
                       self.counter), run_time=0.7)
        plot = Plot(-6.4, -2.45, 13.0, 5.2, (0, len(data)), (0.8, 1.6), 'номер кадра', 'время кадра, мс',
                    xticks=[(f, str(f)) for f in range(0, len(data) + 1, 35)],
                    yticks=[(v, f'{v:.1f}') for v in (1.0, 1.2, 1.4)])
        self.play(plot.appear(), run_time=1.2)
        segs, start = VGroup(), 0
        for f in range(1, len(data) + 1):
            if f == len(data) or data[f][0] != data[start][0]:
                xs = list(range(start, f))
                color = GREEN if data[start][0] else RED
                segs.add(plot.curve(xs, [data[i][1] for i in xs], color, width=2))
                start = f
        legend = plot.legend([('JIT', GREEN), ('Branch', RED)], x=plot.x0 + 10.5)
        self.say('Режимы меняются каждую секунду, игра идёт как обычно.',
                 'Зелёные участки: JIT, красные: Branch.',
                 extra=[LaggedStart(*[Create(seg) for seg in segs], lag_ratio=1, run_time=4),
                        show(legend, lag=0.2)], run_time=4)
        jit = [t for j, t in data if j]
        br = [t for j, t in data if not j]
        speedup = (sum(br) / len(br)) / (sum(jit) / len(jit))
        self.say(f'Ускорение равно среднему времени кадра Branch, делённому на JIT. В модели {speedup:.2f}.',
                 'Вывод: код без лишних чтений памяти и переходов рисует кадр быстрее.')
