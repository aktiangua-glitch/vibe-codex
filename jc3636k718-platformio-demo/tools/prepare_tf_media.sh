#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
    echo "Usage: $0 OUTPUT_DIR [PHOTO_FILE] [VIDEO_FILE]"
    echo "Example: $0 ./tf_card_ready ~/Pictures/photo.jpg ~/Movies/demo.mp4"
    exit 2
fi

output_dir="$1"
photo_file="${2:-}"
video_file="${3:-}"

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ffmpeg is required. Install it first with: brew install ffmpeg"
    exit 1
fi

mkdir -p "$output_dir/pic" "$output_dir/mjpeg"

if [[ -n "$photo_file" ]]; then
    ffmpeg -hide_banner -loglevel warning -y \
        -i "$photo_file" \
        -frames:v 1 \
        -vf "scale=360:360:force_original_aspect_ratio=decrease,pad=360:360:(ow-iw)/2:(oh-ih)/2:black" \
        -c:v mjpeg -pix_fmt yuvj420p -q:v 5 -update 1 \
        "$output_dir/pic/demo.jpg"
else
    ffmpeg -hide_banner -loglevel warning -y \
        -f lavfi -i "testsrc2=size=360x360:rate=1" \
        -frames:v 1 \
        -c:v mjpeg -pix_fmt yuvj420p -q:v 5 -update 1 \
        "$output_dir/pic/demo.jpg"
fi

if [[ -n "$video_file" ]]; then
    ffmpeg -hide_banner -loglevel warning -y \
        -i "$video_file" \
        -an -t 30 \
        -vf "fps=20,scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:(ow-iw)/2:(oh-ih)/2:black" \
        -c:v mjpeg -pix_fmt yuvj420p -q:v 7 -f mjpeg \
        "$output_dir/mjpeg/demo.mjpeg"
else
    ffmpeg -hide_banner -loglevel warning -y \
        -f lavfi -i "testsrc2=size=320x240:rate=20" \
        -an -t 8 \
        -c:v mjpeg -pix_fmt yuvj420p -q:v 7 -f mjpeg \
        "$output_dir/mjpeg/demo.mjpeg"
fi

echo
echo "TF media is ready in: $output_dir"
echo "Copy the pic and mjpeg folders to the root of a FAT32/exFAT TF card."
