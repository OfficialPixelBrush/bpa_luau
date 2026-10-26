# bpa_luau

[Luau](https://luau.org/) integration for the Betrock++ addon system.

## Compilation

Requires a C++17 compiler and CMake 3.16+. Luau is downloaded and built from
source automatically, then linked statically into the addon, so there is nothing
else to install. Works with GCC/Clang on Linux and macOS and MSVC on Windows.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bpa_luau --config Release
```

This produces `bpa_luau.so` (`bpa_luau.dll` on Windows). On Windows with the Visual Studio
generator the DLL ends up in `build/Release/`. To build against an existing Luau checkout instead
of downloading one, add `-DFETCHCONTENT_SOURCE_DIR_LUAU=/path/to/luau`.

`addon_api.h` must match the `include/addon_api.h` of the Betrock++ build you run it on.

### Releases

The `Build & Release Addon` GitHub Actions workflow is run manually and builds Windows
(x86_64, arm64), Linux (x86_64, aarch64) and macOS (arm64) binaries. The tag you enter must match
`ADDON_VERSION` in `src/bpa_luau.cpp`.

See `examples/example.luau` for a script that touches every event.

## Lua API

| Namespace  | Functions                                                                                                                     |
| ---------- | ----------------------------------------------------------------------------------------------------------------------------- |
| `log`      | `info(msg)`, `warning(msg)`, `error(msg)`                                                                                     |
| `server`   | `getPlayerCount()`, `getPlayerAt(index)`, `stop(timeout?)`, `getMsptAverage()`, `getTpsAverage()`, `getVersion()`             |
| `player`   | `sendMessage(player, msg)`, `kick(player, reason?)`, `getUsername(player)`, `getEntity(player)`, `isOperator(player)`         |
| `entity`   | `getPosition(entity)`, `setPosition(entity, x, y, z)`, `getWorld(entity)`                                                     |
| `world`    | `getBlock(world, x, y, z)`, `setBlock(world, x, y, z, id, meta)`, `sendBlockUpdate(world, x, y, z, id, meta)`, `getCurrent()` |
| `data`     | `setPlayer(player, value)`, `getPlayer(player)`                                                                               |
| `commands` | `execute(sender, command)` returns `boolean`; `sender` is a player handle, or `nil` for no player                             |
| `file`     | `read(name)`, `write(name, text)`, `append(name, text)`, `remove(name)`, `exists(name)`                                       |

`file.*` only works inside `luau/data/`: names are relative to it, subfolders are created as needed, and absolute
paths or `..` raise an error. `read` returns the text or `nil`; the others return `true` on success.

`player.isOperator(player)` checks `ops.txt` the same way the server does.

Script callbacks: `OnPlayerJoin`, `OnPlayerLeave`, `OnPlayerChat`, `OnPlayerMove`, `OnItemUse`,
`OnBlockUse`, `OnBlockBreak`, `OnBlockPlace`, `OnEntityDamage`, `OnServerTick`, `OnCrash`, `OnUnload`.

`OnCrash(crashLog, crashLogPath, version)` receives strings (or `nil` for any the server leaves unset) and can't cancel anything.
