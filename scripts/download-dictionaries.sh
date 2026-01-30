#!/bin/bash
# Download StarDict dictionaries for the Dictionary app
# Source: WikDict (https://www.wikdict.com/) - CC BY-SA 3.0 licensed

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DICT_DIR="$SCRIPT_DIR/../dictionaries"

echo "Downloading StarDict dictionaries..."

# Create directories
mkdir -p "$DICT_DIR/fr-en" "$DICT_DIR/en-fr"

# Download French-English (121,037 words)
echo "Downloading French -> English dictionary..."
curl -L "https://download.wikdict.com/dictionaries/stardict/wikdict-fr-en.zip" -o "$DICT_DIR/fr-en.zip"
unzip -o "$DICT_DIR/fr-en.zip" -d "$DICT_DIR/fr-en"
mv "$DICT_DIR/fr-en/wikdict-fr-en/"* "$DICT_DIR/fr-en/"
rmdir "$DICT_DIR/fr-en/wikdict-fr-en"
rm "$DICT_DIR/fr-en.zip"

# Download English-French (72,563 words)
echo "Downloading English -> French dictionary..."
curl -L "https://download.wikdict.com/dictionaries/stardict/wikdict-en-fr.zip" -o "$DICT_DIR/en-fr.zip"
unzip -o "$DICT_DIR/en-fr.zip" -d "$DICT_DIR/en-fr"
mv "$DICT_DIR/en-fr/wikdict-en-fr/"* "$DICT_DIR/en-fr/"
rmdir "$DICT_DIR/en-fr/wikdict-en-fr"
rm "$DICT_DIR/en-fr.zip"

echo ""
echo "Done! Dictionaries downloaded to: $DICT_DIR"
echo ""
echo "To use on device, copy the dictionaries/ folder to SD card root."
