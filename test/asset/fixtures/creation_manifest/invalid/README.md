# Invalid creation-manifest fixtures

Each JSON file is a complete manifest that must fail validation. The portion
before \`__\` in its filename is the key path the diagnostic must name.

- \`targets[0].name__*.json\`: invalid target syntax or a reserved Windows
  device stem.
- \`targets[0].display__*.json\` and \`targets[0].description__*.json\`: a
  non-string optional target label.
- \`targets[0].package__*.json\`: a non-boolean package flag.
- \`targets[0].presets[0].name__*.json\`: a missing, empty, or non-string
  required preset name.
- \`thumbnail__*.json\`: an empty, NUL-containing, absolute, or escaping
  creation-relative thumbnail path.
- \`docs[0]__*.json\`: an empty, NUL-containing, absolute, or escaping
  checkout-root-relative documentation path.

\`../accepted_target_names.json\` is the positive control for names that
resemble devices but are valid: \`NULL\`, \`LPT10\`, \`nul_x\`, and \`x.nul\`.
