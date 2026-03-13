#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <mounted_sd_path>"
  exit 1
fi

SD_PATH="$1"
if [[ ! -d "$SD_PATH" ]]; then
  echo "Path not found: $SD_PATH"
  exit 1
fi

mkdir -p "$SD_PATH/www"
find "$SD_PATH/www" -mindepth 1 -maxdepth 1 -type f -delete
cp -rf data/www/. "$SD_PATH/www/"

echo "Copied web UI to $SD_PATH/www"
