#!/bin/sh
# Render a manim scene in parallel chunks of animations, then join them without re-encoding.
# Usage: tools/manim-par.sh MANIM MEDIA_DIR OUT.mp4 FILE.py SCENE [manim flags...]
# JOBS processes at once (default 8), CHUNKS pieces (default 2 * JOBS).
# The scene writes its number of animations to $MANIM_COUNT; the last chunk is open-ended,
# so the count of the previous render is enough to split the next one.
set -e
manim=$1 media=$2 out=$3 file=$4 scene=$5
shift 5
flags="$*"
jobs=${JOBS:-8}
chunks=${CHUNKS:-$((jobs * 2))}
rm -rf "$media/par" && mkdir -p "$media/par"

if [ ! -s "$media/count" ]; then
	# First render: count the animations, skipping all of them
	MANIM_COUNT="$media/count" $manim render $flags --disable_caching --media_dir "$media/par/count.d" \
		-n 999999 -o count "$file" "$scene" > "$media/par/count.log" 2>&1 || true
	[ -s "$media/count" ] || { tail -20 "$media/par/count.log"; exit 1; }
fi
total=$(cat "$media/count")
echo "animations: about $total, chunks: $chunks, jobs: $jobs"

# Chunk i covers animations a..b inclusive, the last one runs to the end
i=0
while [ $i -lt "$chunks" ]; do
	a=$((total * i / chunks)) b=$((total * (i + 1) / chunks - 1))
	[ $i -eq $((chunks - 1)) ] && b=end
	[ "$b" = end ] || [ $b -ge $a ] && echo "$i $a $b"
	i=$((i + 1))
done > "$media/par/chunks"

export manim media file scene flags
xargs -P "$jobs" -L 1 sh -c '
	d="$media/par/$0"
	range="$1,$2"
	[ "$2" = end ] && range=$1
	if ! MANIM_COUNT="$media/count.new" nice $manim render $flags --disable_caching --media_dir "$d" \
		-n "$range" -o part "$file" "$scene" > "$d.log" 2>&1 && grep -q Traceback "$d.log"; then
		echo "chunk $0 failed, see $d.log"
		exit 255
	fi
	echo "chunk $0 ($1-$2) done"
' < "$media/par/chunks"
mv "$media/count.new" "$media/count"

# A chunk past the end of a shorter scene renders nothing
while read -r i a b; do
	f=$(find "$media/par/$i" -name part.mp4)
	[ -n "$f" ] && echo "file '$f'"
done < "$media/par/chunks" > "$media/par/list.txt"
ffmpeg -loglevel error -y -f concat -safe 0 -i "$media/par/list.txt" -c copy "$out"
echo "written $out"
