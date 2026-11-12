# Films about the experiments: manim scenes, one per experiment.
# Not part of make all in the project root: students do not need manim.
# manim needs cairo, pango and pkgconf (Homebrew) and ffmpeg.
VENV  := venv
MANIM ?= $(VENV)/bin/manim
OUT   ?= out
MEDIA ?= /tmp/memorylab-films
JOBS  ?= 6
FPS   ?= 30
N     ?= 1
FILMS ?= 1 2 3 4 5 6 7 8

SCENE_1 := exp1_dram.py Exp1Dram
SCENE_2 := exp2_list.py Exp2List
SCENE_3 := exp3_tlb.py Exp3Tlb
SCENE_4 := exp4_streams.py Exp4Streams
SCENE_5 := exp5_conflicts.py Exp5Conflicts
SCENE_6 := exp6_cache.py Exp6Cache
SCENE_7 := exp7_sort.py Exp7Sort
SCENE_8 := exp8_jit.py Exp8Jit

.PHONY: all install test preview previews film films clean

all: films

install:
	python3.12 -m venv $(VENV) && $(VENV)/bin/pip install -U pip && $(VENV)/bin/pip install -r requirements.txt

# The models give the curves of the films; the test checks their shape
test:
	python3.12 test_model.py

# Draft of film N: 360p, 12 frames per second
preview:
	mkdir -p $(OUT) && JOBS=$(JOBS) tools/manim-par.sh $(MANIM) $(MEDIA)/$(N)-preview \
		$(OUT)/exp$(N)-preview.mp4 $(SCENE_$(N)) --resolution 640,360 --frame_rate 12

previews:
	for n in $(FILMS); do $(MAKE) --no-print-directory preview N=$$n || exit 1; done

# Film N: 1080p
film:
	mkdir -p $(OUT) && JOBS=$(JOBS) tools/manim-par.sh $(MANIM) $(MEDIA)/$(N) \
		$(OUT)/exp$(N).mp4 $(SCENE_$(N)) --resolution 1920,1080 --frame_rate $(FPS)

films:
	for n in $(FILMS); do $(MAKE) --no-print-directory film N=$$n || exit 1; done

clean:
	rm -rf $(OUT) $(MEDIA) __pycache__
