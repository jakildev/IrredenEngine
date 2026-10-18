# Invalid creation-manifest fixtures

Each JSON file is a complete manifest that must fail validation. The portion
before \`__\` in its filename is the key path the diagnostic must name.

- \`targets[0].name__*.json\`: invalid target syntax or a reserved Windows
  device stem.
- \`thumbnail__*.json\`: an empty, NUL-containing, absolute, or escaping
  creation-relative thumbnail path.
- \`docs[0]__*.json\`: an empty, NUL-containing, absolute, or escaping
  checkout-root-relative documentation path.

\`../accepted_target_names.json\` is the positive control for names that
resemble devices but are valid: \`NULL\`, \`LPT10\`, \`nul_x\`, and \`x.nul\`.
