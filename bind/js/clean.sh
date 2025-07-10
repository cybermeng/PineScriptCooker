#!/bin/bash

# clean.sh - Removes the build artifacts directory.

OUTPUT_DIR="public"

if [ -d "$OUTPUT_DIR" ]; then
    echo "Removing directory: $OUTPUT_DIR"
    rm -rf "$OUTPUT_DIR"
    echo "Cleanup complete."
else
    echo "Directory $OUTPUT_DIR does not exist. Nothing to clean."
fi