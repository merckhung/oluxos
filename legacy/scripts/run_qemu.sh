#!/bin/bash
IMAGE_PATH=$1

if [ -z "$IMAGE_PATH" ]; then
    echo "Usage: $0 <path_to_image>"
    exit 1
fi

echo "Booting OluxOS image at $IMAGE_PATH with QEMU..."
shift
qemu-system-i386 -drive file="$IMAGE_PATH",format=raw,snapshot=on -m 256 "$@"
