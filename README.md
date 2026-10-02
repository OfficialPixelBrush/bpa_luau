# bpa_luau
[Luau](https://luau.org/) integration for the Betrock++ addon system.

## Compilation

Since Luau isn't shipped by most Distros or similar, it needs to get compiled from scratch first.

```bash
git clone https://github.com/luau-lang/luau.git
cd luau
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DLUAU_BUILD_CLI=OFF -DLUAU_BUILD_TESTS=OFF
cmake --build build --target Luau.VM Luau.Compiler Luau.Ast Luau.Bytecode Luau.Common
```

Then build the addon, pointing at that checkout (`LUAU=/path/to/luau`,
`LIBS=$LUAU/build`; on Windows/MSVC the library layout differs):

### Linux

```bash
LUAU=/path/to/luau
g++ -std=c++17 -shared -fPIC src/bpa_luau.cpp -o bpa_luau.so \
    -I$LUAU/VM/include -I$LUAU/Compiler/include \
    $LUAU/build/libLuau.Compiler.a $LUAU/build/libLuau.Bytecode.a \
    $LUAU/build/libLuau.Ast.a $LUAU/build/libLuau.VM.a $LUAU/build/libLuau.Common.a
```

### macOS

```bash
LUAU=/path/to/luau
g++ -std=c++17 -shared -fPIC -undefined dynamic_lookup src/bpa_luau.cpp -o bpa_luau.so \
    -I$LUAU/VM/include -I$LUAU/Compiler/include \
    $LUAU/build/libLuau.Compiler.a $LUAU/build/libLuau.Bytecode.a \
    $LUAU/build/libLuau.Ast.a $LUAU/build/libLuau.VM.a $LUAU/build/libLuau.Common.a
```

### Windows (MSYS2 MinGW64 shell)

```bash
LUAU=/path/to/luau
g++ -std=c++17 -shared src/bpa_luau.cpp -o bpa_luau.dll \
    -I$LUAU/VM/include -I$LUAU/Compiler/include \
    $LUAU/build/libLuau.Compiler.a $LUAU/build/libLuau.Bytecode.a \
    $LUAU/build/libLuau.Ast.a $LUAU/build/libLuau.VM.a $LUAU/build/libLuau.Common.a
```

See `examples/example.luau` for a script that touches every event.
