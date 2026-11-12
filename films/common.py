"""Shared style and helpers of the memorylab films.

The films follow the pipeline-anim videos of the course: GOST Type BU letters,
Catppuccin Mocha colours on a dark background, two caption lines at the bottom.
A film is a Scene subclass of Film: it sets TITLE and writes story().
"""
import os
from pathlib import Path

import numpy as np
from manim import (BOLD, DOWN, ITALIC, LEFT, RIGHT, UP, Animation, AnimationGroup, Create, DashedLine, Dot, FadeIn,
                   FadeOut, LaggedStart, Line, Mobject, Rectangle, RoundedRectangle, Scene, SurroundingRectangle, Text,
                   VGroup, VMobject, Write, config, there_and_back)
from manim.renderer.cairo_renderer import CairoRenderer

FONT, MONO = 'GOST Type BU', 'PT Mono'
BG, PANEL, TEXT, SUB = '#242424', '#1e1e2e', '#cdd6f4', '#a6adc8'
DIM, LINE, SURF, HL = '#9399b2', '#585b70', '#313244', '#f5e0dc'
RED, GREEN, BLUE, YELLOW = '#f38ba8', '#a6e3a1', '#89b4fa', '#f9e2af'
PEACH, MAUVE, TEAL, PINK, SKY, LAVENDER = '#fab387', '#cba6f7', '#94e2d5', '#f5c2e7', '#89dceb', '#b4befe'

config.background_color = BG
# x264 at the default crf 23 smears thin light letters on the dark background
config.video_encoder_options = {'crf': '12', 'preset': 'slow', 'tune': 'animation'}

CAP1_Y, CAP2_Y, CAP_X = -3.32, -3.72, -6.8
TITLE_Y = 3.55


def _not_when_skipping(draw):
    """manim -n still draws every skipped animation; the parallel render skips most of
    the film in every chunk, so drawing is dropped there."""
    def wrapper(self, *args, **kwargs):
        if self.skip_animations and not self.file_writer.output_spec.is_still:
            self.static_image = None
            return None
        return draw(self, *args, **kwargs)
    return wrapper


CairoRenderer.update_frame = _not_when_skipping(CairoRenderer.update_frame)
CairoRenderer.save_static_frame_data = _not_when_skipping(CairoRenderer.save_static_frame_data)


def txt(s, size=18, color=TEXT):
    # GOST Type BU has one weight: an outline in the text colour thickens the letters
    t = Text(s, font=FONT, font_size=size, color=color, weight=BOLD, slant=ITALIC)
    # Every animation lifts its mobject to the top: a cell that blinks would cover its label
    return t.set_stroke(color, width=size * 0.03, opacity=1).set_z_index(3)


def mono(s, size=16, color=TEXT):
    return Text(s, font=MONO, font_size=size, color=color).set_z_index(3)


def code(lines, x, y, size=15, dy=0.3):
    """Program text, left-aligned from (x, y) down; a line is a str or (str, colour)."""
    g = VGroup()
    char = mono('0' * 20, size).width / 20       # Text drops leading spaces: indent by hand
    for i, ln in enumerate(lines):
        s, c = (ln, TEXT) if isinstance(ln, str) else ln
        indent = len(s) - len(s.lstrip(' '))
        g.add(mono(s.strip(' '), size, c).move_to((x + indent * char, y - i * dy, 0), aligned_edge=LEFT))
    return g


def cell(x, y, w, h, color=SURF, opacity=1.0, stroke=LINE):
    return Rectangle(width=w, height=h, stroke_color=stroke, stroke_width=1.5, fill_color=color,
                     fill_opacity=opacity).move_to((x, y, 0))


def panel(x, y, w, h, title='', size=17):
    """A rounded frame with a title on top."""
    b = RoundedRectangle(corner_radius=0.08, width=w, height=h, stroke_color=SUB, stroke_width=2)
    b.move_to((x, y, 0))
    g = VGroup(b)
    if title:
        g.add(txt(title, size, SUB).next_to(b, UP, buff=0.08))
    return g


def polyline(points, color, width=3):
    return VMobject(stroke_color=color, stroke_width=width).set_points_as_corners(
        [np.array((a, b, 0.0)) for a, b in points])


# ==========================
# ===  Motion            ===
# ==========================
# The films move like the pipeline-anim videos: parts slide in one after another,
# leave with a short slide, labels roll over, dots grow from their centre.
# Create is kept for lines and frames: on text it traces every letter.

def _kw(run_time):
    return {} if run_time is None else {'run_time': run_time}


def parts(group):
    """Top-level pieces of a group; a text or a single shape stays whole."""
    if isinstance(group, Text) or not isinstance(group, VGroup) or not group.submobjects:
        return [group]
    return list(group.submobjects)


def show(*groups, lag=0.08, shift=UP * 0.15, run_time=None):
    """Pieces fade in one after another with a short slide up."""
    pieces = [q for g in groups for q in parts(g)]
    shift = np.array(shift, dtype=float)       # a tuple times -1 would turn into an empty tuple
    return LaggedStart(*[FadeIn(q, shift=shift) for q in pieces], lag_ratio=lag, **_kw(run_time))


def hide(*mobs, run_time=None):
    return AnimationGroup(*[FadeOut(m, shift=DOWN * 0.1) for m in mobs], **_kw(run_time))


def draw(*groups, run_time=None):
    """Lines and frames are drawn, texts fade in."""
    anims = []

    def walk(m):
        if isinstance(m, Text):
            anims.append(FadeIn(m))
        elif isinstance(m, VGroup) and m.submobjects:
            for q in m.submobjects:
                walk(q)
        else:
            anims.append(Create(m))
    for g in groups:
        walk(g)
    return AnimationGroup(*anims, **_kw(run_time))


def pop(mob, run_time=None):
    """A dot or a mark grows from its centre."""
    return FadeIn(mob, scale=0.2, **_kw(run_time))


def roll(old, new, dy=0.12):
    """Replace a label in place: the old one leaves upward, the new one comes up from below.
    dy=0 cross-fades, for labels stacked close to each other."""
    return [FadeOut(old, shift=UP * dy), FadeIn(new, shift=UP * dy)]


def blink(mob, color, opacity=0.8):
    """Light a shape up and back in one smooth move."""
    return mob.animate(rate_func=there_and_back).set_fill(color, opacity)


class Fade(Animation):
    """Fade by opacity alone: the stock FadeIn copies the mobject three times. The old
    text fades in the first half, the new one in the second, so they never overlap."""

    def __init__(self, mob, appear, **kw):
        super().__init__(mob, introducer=appear, remover=not appear, **kw)
        self.appear = appear

    def begin(self):
        self.starting_mobject = Mobject()
        self.interpolate(0)

    def interpolate_mobject(self, alpha):
        a = self.rate_func(alpha)
        self.mobject.set_opacity(max(0, 2 * a - 1) if self.appear else max(0, 1 - 2 * a))


# ==========================
# ===  Plot              ===
# ==========================

class Plot(VGroup):
    """Axes in a box with corner (x0, y0) and size w x h; data coordinates map linearly
    or, with xlog, by log2 on x."""

    def __init__(self, x0, y0, w, h, xr, yr, xlabel, ylabel, xticks=(), yticks=(), xlog=False):
        super().__init__()
        self.x0, self.y0, self.w, self.h, self.xr, self.yr, self.xlog = x0, y0, w, h, xr, yr, xlog
        self.lines, self.labels = VGroup(), VGroup()
        self.lines.add(Line((x0, y0, 0), (x0 + w, y0, 0), color=SUB, stroke_width=2),
                       Line((x0, y0, 0), (x0, y0 + h, 0), color=SUB, stroke_width=2))
        for v, label in xticks:
            px = self.at(v, yr[0])[0]
            self.lines.add(DashedLine((px, y0, 0), (px, y0 + h, 0), color=LINE, stroke_width=1, dash_length=0.06))
            self.labels.add(txt(label, 13, DIM).move_to((px, y0 - 0.2, 0)))
        for v, label in yticks:
            py = self.at(xr[0], v)[1]
            self.lines.add(DashedLine((x0, py, 0), (x0 + w, py, 0), color=LINE, stroke_width=1, dash_length=0.06))
            self.labels.add(txt(label, 13, DIM).move_to((x0 - 0.1, py, 0), aligned_edge=RIGHT))
        self.labels.add(txt(xlabel, 15, SUB).move_to((x0 + w, y0 - 0.48, 0), aligned_edge=RIGHT),
                        txt(ylabel, 15, SUB).move_to((x0, y0 + h + 0.22, 0), aligned_edge=LEFT))
        self.add(self.lines, self.labels)

    def appear(self, run_time=1.0):
        """Axes and grid are drawn, labels fade in after them."""
        return AnimationGroup(Create(self.lines), show(self.labels, lag=0.05), lag_ratio=0.4, run_time=run_time)

    def _fx(self, x):
        a, b = self.xr
        if self.xlog:
            x, a, b = np.log2(x), np.log2(a), np.log2(b)
        return (x - a) / (b - a)

    def at(self, x, y):
        a, b = self.yr
        return np.array((self.x0 + self._fx(x) * self.w, self.y0 + (y - a) / (b - a) * self.h, 0.0))

    def curve(self, xs, ys, color, width=3):
        return polyline([self.at(x, y)[:2] for x, y in zip(xs, ys)], color, width)

    def dot(self, x, y, color, r=0.06):
        return Dot(self.at(x, y), radius=r, color=color)

    def mark(self, x, label, color=HL, size=15, top=True):
        """A vertical line at x with its label inside the box, right of the top or bottom end."""
        px = self.at(x, self.yr[0])[0]
        ly = self.y0 + self.h - 0.15 if top else self.y0 + 0.18
        return VGroup(Line((px, self.y0, 0), (px, self.y0 + self.h, 0), color=color, stroke_width=2),
                      txt(label, size, color).move_to((px + 0.1, ly, 0), aligned_edge=LEFT))

    def legend(self, items, x=None, y=None, size=14):
        """Coloured strokes with names, from the top-left corner of the box down."""
        x = self.x0 + 0.25 if x is None else x
        y = self.y0 + self.h - 0.2 if y is None else y
        g = VGroup()
        for i, (name, color) in enumerate(items):
            yy = y - i * 0.3
            g.add(Line((x, yy, 0), (x + 0.35, yy, 0), color=color, stroke_width=4),
                  txt(name, size, color).move_to((x + 0.45, yy, 0), aligned_edge=LEFT))
        return g


# ==========================
# ===  Film              ===
# ==========================

class Film(Scene):
    """Base scene: a title, captions, attention frames, the count for tools/manim-par.sh."""
    TITLE = ''

    def construct(self):
        self.capt = VGroup()
        self.head = txt(self.TITLE, 26, TEXT).move_to((-6.8, TITLE_Y, 0), aligned_edge=LEFT)
        self.play(Write(self.head), run_time=1.0)
        self.story()
        self.wait(0.5)
        self.play(*[FadeOut(m) for m in self.mobjects], run_time=0.8)
        self.wait(0.5)
        if os.environ.get('MANIM_COUNT'):
            Path(os.environ['MANIM_COUNT']).write_text(str(self.renderer.num_plays))

    def story(self):
        raise NotImplementedError

    def caption(self, line, second='', color=TEXT):
        g = VGroup(txt(line, 21, color).move_to((CAP_X, CAP1_Y, 0), aligned_edge=LEFT))
        if second:
            g.add(txt(second, 19, SUB).move_to((CAP_X, CAP2_Y, 0), aligned_edge=LEFT))
        return g.set_z_index(6)

    def say(self, line, second='', color=TEXT, extra=(), hold=None, run_time=0.6):
        """Replace the caption and keep it on screen long enough to read."""
        g = self.caption(line, second, color)
        self.play(Fade(self.capt, False), Fade(g, True), *extra, run_time=run_time)
        self.capt = g
        if hold is None:
            hold = 0.6 + (len(line) + len(second)) / 24
        if hold > 0:
            self.wait(hold)

    def recode(self, prog, lines, x, y, **kw):
        """A listing with new lines in place of `prog`: changed lines roll over, the rest stay.
        Returns the new listing and the animations."""
        new = code(lines, x, y, **kw)
        anims = []
        for i, (old, line) in enumerate(zip(prog, new)):
            if old.text == line.text and old.get_color() == line.get_color():
                new.submobjects[i] = old
            else:
                anims += roll(old, line, dy=0.2)
        return new, anims

    def frame(self, mob, color=HL, buff=0.08):
        return SurroundingRectangle(mob, color=color, buff=buff, corner_radius=0.08, stroke_width=3).set_z_index(8)

    def spot(self, mobs, line, second='', hold=None):
        """Frame the parts, say what they show, drop the frames after reading."""
        frames = VGroup(*[self.frame(m) for m in mobs])
        self.say(line, second, extra=[LaggedStart(*[Create(f) for f in frames], lag_ratio=0.1)], hold=hold)
        self.play(FadeOut(frames), run_time=0.4)
