#!/bin/bash
XBOX_IP="${XBOX_IP:-192.168.3.211}"
XBOX_USER="${XBOX_USER:-xbox}"
XBOX_PASS="${XBOX_PASS:-xbox}"
REMOTE_PATH="/F/Apps/testing/default.xbe"
LOCAL_FILE="out/default.xbe"

if [ ! -f "$LOCAL_FILE" ]; then
    echo "Error: $LOCAL_FILE not found"
    exit 1
fi

echo "Deploying $(du -h "$LOCAL_FILE" | cut -f1) to $XBOX_IP..."
curl -T "$LOCAL_FILE" "ftp://${XBOX_USER}:${XBOX_PASS}@${XBOX_IP}/${REMOTE_PATH}" --ftp-create-dirs

if [ $? -eq 0 ]; then
    echo "Done."
else
    echo "Upload failed."
    exit 1
fi
