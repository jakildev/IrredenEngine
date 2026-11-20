# Label state is stored in files so concurrent stub processes do not contend on
# a shared directory lock. Escape the filename delimiter for native Windows.
race_labels_path() {
    local label="${2//%/%25}"
    label="${label//:/%3A}"
    printf '%s/%s' "$1" "$label"
}

race_labels_reset() {
    rm -rf "$1"
    mkdir -p "$1"
}

race_labels_add() {
    mkdir -p "$1"
    : > "$(race_labels_path "$1" "$2")"
}

race_labels_remove() {
    rm -f "$(race_labels_path "$1" "$2")"
}

race_labels_list() {
    local path label
    [[ -d "$1" ]] || return 0
    for path in "$1"/*; do
        [[ -f "$path" ]] || continue
        label="${path##*/}"
        label="${label//%3A/:}"
        printf '%s\n' "${label//%25/%}"
    done
}

race_labels_emit_json() {
    local first=1 label
    printf '['
    while IFS= read -r label; do
        [[ -n "$label" ]] || continue
        [[ "$first" -eq 1 ]] || printf ','
        printf '{"name":"%s"}' "$label"
        first=0
    done < <(race_labels_list "$1")
    printf ']'
}
