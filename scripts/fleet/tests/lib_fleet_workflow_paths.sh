#!/usr/bin/env bash

# fleet_coverage_list <file> — the dorny detector's covered paths.
fleet_coverage_list() {
    awk '
        /^            covered:$/ { in_filter=1; next }
        in_filter && /^              - / { sub(/^              - /, ""); print; next }
        in_filter && NF { exit }
    ' "$1"
}
