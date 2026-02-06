#!/bin/bash
# Script to insert or update header comments in all .c and .h files

HEADER_FILE="header.txt"

if [[ ! -f "$HEADER_FILE" ]]; then
    echo "Error: $HEADER_FILE not found"
    exit 1
fi

# Read the header content
HEADER=$(<"$HEADER_FILE")

# Function to update header in a file
update_header() {
    local file="$1"

    if [[ ! -f "$file" ]]; then
        return
    fi

    # Check if file already starts with the correct header
    if head -c ${#HEADER} "$file" | diff -q - "$HEADER_FILE" > /dev/null 2>&1; then
        echo "✓ $file (already has correct header)"
        return
    fi

    # Create a temporary file
    local tmpfile=$(mktemp)

    # Check if file starts with a comment block
    if head -n 1 "$file" | grep -q '^/\*'; then
        # Find where the comment block ends
        local end_line=$(awk '/\*\// {print NR; exit}' "$file")
        if [[ -n "$end_line" ]]; then
            # Replace the existing comment block
            {
                cat "$HEADER_FILE"
                tail -n +$((end_line + 1)) "$file"
            } > "$tmpfile"
        else
            # Malformed comment, just prepend
            {
                cat "$HEADER_FILE"
                cat "$file"
            } > "$tmpfile"
        fi
    else
        # No comment block at start, just prepend the header
        {
            cat "$HEADER_FILE"
            cat "$file"
        } > "$tmpfile"
    fi

    # Replace the original file
    mv "$tmpfile" "$file"
    echo "✓ $file (header updated)"
}

# Find and update all .c and .h files
echo "Updating headers in .c and .h files..."
echo

for file in *.c *.h; do
    if [[ -f "$file" ]]; then
        update_header "$file"
    fi
done

echo
echo "Done!"
