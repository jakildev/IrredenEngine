# engine/prefabs/irreden/demo/ — example/template prefabs

Minimal example prefabs used in documentation, tests, and as copy-paste
templates when adding a new component or entity type. **Do not include
these in production creations.**

## Contents

- `components/component_example.hpp` — `C_Example`. A single
  `std::string exampleSentence_` member. Used only as a template to
  show the `C_` component struct layout.
- `entities/entity_example.hpp` — `Prefab<PrefabTypes::kExample>`.
  Creates an entity with `C_Example{}`. Shows the `template<> struct
  Prefab<T>` pattern and how to call `entity.set(...)`.

## Rules

- **Don't copy from `demo/` into real code.** The `C_Example` component
  and `kExample` prefab exist solely as readable references. Real
  components belong under the domain that owns them (`common/`,
  `update/`, `voxel/`, etc.).
- **Don't add logic here.** If an example needs more than a trivial
  struct, it has grown beyond this directory's purpose. Put it in the
  right domain directory instead.
- **No production includes.** Nothing outside `demo/` should `#include`
  from `demo/`. These headers are not part of any umbrella `ir_*.hpp`.
