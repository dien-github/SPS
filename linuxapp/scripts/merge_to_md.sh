#!/bin/bash

TARGET_DIR="${1:-.}"
VERSION="$2"
OUTPUT="source_code.md"

if [ ! -d "$TARGET_DIR" ]; then
	echo "[-] Directory $TARGET_DIR not exist!"
fi

if [ -n "$VERSION" ]; then
        OUTPUT="source_code_${VERSION}.md"
else
        TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
        OUTPUT="source_code_${TIMESTAMP}.md"
fi

echo "[+] Synthesis source code" > "$OUTPUT"
echo "Created time: $(date)" >> "$OUTPUT"

find "$TARGET_DIR" -type f \( -name "*.h" -o -name "*.cpp" -o -name "*.xml" -o -name "*.txt" \) | while read -r file; do
	echo "Process: $file"
	ext="${file##*.}"
	if [ "$ext" = "h" ]; then
		language="cpp"
	else
		language="$ext"
	fi

	{
		echo ""
		echo "## File: \`$file\`"
		echo "---"
		echo "\`\`\`$language"
		cat "$file"
		echo ""
		echo "\`\`\`"
	} >> "$OUTPUT"
done

echo "---"
echo "Done."
