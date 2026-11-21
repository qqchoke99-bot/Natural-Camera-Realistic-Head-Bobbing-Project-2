# Default config

`config.json` and `config.schema.json` are checked in because they are generated
on a **host** machine: `src/config_generator.cpp` builds against the host
compiler, and a cross-compiled (arm64) helper cannot run on the CI runner.

Regenerate after changing `src/mod/Config.h`:

```sh
g++ -std=c++20 -Isrc -o /tmp/genconfig src/config_generator.cpp src/mod/Config.cpp
/tmp/genconfig config/config.json config/config.schema.json
```

CMake copies these two files into every package; the mod rewrites
`config/config.json` on first launch if it is missing.
