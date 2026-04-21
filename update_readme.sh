#!/bin/bash
for file in readme_assets/galactic_objects/*.png; do
    filename=$(basename "$file")
    # Extract the old name (assuming based on the previous sed logic, 
    # but to be safe, I'll search for the file pattern in README)
    # The previous sed commands relied on the old full names.
    # I will perform a search and replace for each file.
    echo "Processing $filename..."
done
