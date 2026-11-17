# Label-state compatibility for the claim-race positive control. The control
# keeps lib_claim_race.sh from the baseline ref, so this new helper supplies the
# atomic state API to both the suite and its gh subprocesses on either tree.

if ! declare -F race_labels_reset >/dev/null; then
    race_labels_reset() {
        rm -rf "$1"
        mkdir -p "$1"
    }

    race_labels_add() {
        mkdir -p "$1"
        : > "$1/$2"
    }

    race_labels_remove() {
        rm -f "$1/$2"
    }

    race_labels_list() {
        local path
        [[ -d "$1" ]] || return 0
        for path in "$1"/*; do
            [[ -f "$path" ]] && printf '%s\n' "${path##*/}"
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
fi
