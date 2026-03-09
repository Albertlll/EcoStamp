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
cp -f data/www/index.html data/www/styles.css data/www/app.js "$SD_PATH/www/"

echo "Copied web UI to $SD_PATH/www"
