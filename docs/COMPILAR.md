# Como compilar o UniEmu

Este guia gera o mesmo pacote publicado em *Releases*: `UniEmu.exe` + núcleos + pastas de apoio.

## 1. Pré-requisitos

| Item | Versão usada |
|---|---|
| CMake | 3.16 ou superior |
| Compilador | MinGW-w64 (GCC 12+, *threads* posix) ou GCC/Clang no Linux |
| SDL2 | 2.30.x (no Windows é ligado estaticamente) |
| Git | para baixar os núcleos |

Tudo o que fica em `third_party/` já vem no repositório: Dear ImGui 1.91.9, miniz 3.0.2, stb_image,
AMD FSR 1.0, shader xBR, fonte Press Start 2P e SDL_GameControllerDB.

## 2. Compilar o UniEmu.exe

### Opção A – No Windows, com MSYS2

1. Instale o [MSYS2](https://www.msys2.org) e abra o terminal **MSYS2 MINGW64**.
2. Instale as ferramentas:
   ```bash
   pacman -S --needed git mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-SDL2
   ```
3. Clone e compile:
   ```bash
   git clone https://github.com/<seu-usuario>/UniEmu.git
   cd UniEmu
   cmake -B build -G "MinGW Makefiles" -DSDL2_ROOT=/mingw64
   cmake --build build -j
   ```
4. O executável fica em `build/UniEmu.exe`.

### Opção B – No Linux, gerando o .exe para Windows (cross-compilação)

1. Instale o MinGW:
   ```bash
   sudo apt install cmake g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64
   ```
2. Baixe `SDL2-devel-2.30.9-mingw.tar.gz` em <https://github.com/libsdl-org/SDL/releases> e extraia.
3. Compile usando o arquivo de *toolchain* que já está no repositório:
   ```bash
   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw64.cmake \
         -DSDL2_ROOT=$HOME/SDL2-2.30.9/x86_64-w64-mingw32
   cmake --build build-win -j
   x86_64-w64-mingw32-strip build-win/UniEmu.exe   # opcional: deixa o .exe menor
   ```

### Opção C – Linux nativo (para desenvolver e testar)

```bash
sudo apt install build-essential cmake libsdl2-dev
cmake -B build && cmake --build build -j
./build/UniEmu
```

## 3. Núcleos libretro (pasta `cores`)

**Jeito mais fácil:** baixe os núcleos prontos do servidor oficial do libretro,
`https://buildbot.libretro.com/nightly/windows/x86_64/latest/<núcleo>.dll.zip`, ou use o botão
**Baixar núcleo** dentro do UniEmu (aba *BIOS e núcleos*).

| Arquivo esperado em `cores\` | Repositório |
|---|---|
| `flycast_libretro.dll` | https://github.com/flyinghead/flycast |
| `mednafen_psx_hw_libretro.dll` | https://github.com/libretro/beetle-psx-libretro |
| `pcsx_rearmed_libretro.dll` | https://github.com/libretro/pcsx_rearmed |
| `mupen64plus_next_libretro.dll` | https://github.com/libretro/mupen64plus-libretro-nx |
| `snes9x_libretro.dll` | https://github.com/libretro/snes9x |
| `genesis_plus_gx_libretro.dll` | https://github.com/libretro/Genesis-Plus-GX |
| `stella_libretro.dll` | https://github.com/stella-emu/stella |

**Para compilar você mesmo** (no MSYS2 MINGW64, ou no Linux acrescentando
`CC=x86_64-w64-mingw32-gcc-posix CXX=x86_64-w64-mingw32-g++-posix`):

```bash
# Dreamcast
git clone --recursive https://github.com/flyinghead/flycast
cmake -S flycast -B flycast/build -DLIBRETRO=ON -DCMAKE_BUILD_TYPE=Release && cmake --build flycast/build -j

# PlayStation (versão com renderização por hardware)
git clone https://github.com/libretro/beetle-psx-libretro
make -C beetle-psx-libretro HAVE_HW=1 platform=win64 -j

# PlayStation sem BIOS
git clone https://github.com/libretro/pcsx_rearmed
make -C pcsx_rearmed -f Makefile.libretro platform=win64 -j

# Nintendo 64
git clone https://github.com/libretro/mupen64plus-libretro-nx
make -C mupen64plus-libretro-nx platform=win64 -j

# Super Nintendo
git clone https://github.com/libretro/snes9x
make -C snes9x/libretro platform=win64 -j

# Mega Drive
git clone https://github.com/libretro/Genesis-Plus-GX
make -C Genesis-Plus-GX -f Makefile.libretro platform=win64 -j

# Atari 2600
git clone https://github.com/stella-emu/stella
make -C stella/src/os/libretro platform=win64 -j
```

Cada núcleo tem suas próprias instruções no README do repositório. Os commits usados no pacote oficial
estão em [`dist/LICENCAS.txt`](../dist/LICENCAS.txt).

Ao compilar com MinGW de *threads* posix, copie também `libwinpthread-1.dll` (fica em
`/mingw64/bin` ou `/usr/lib/gcc/x86_64-w64-mingw32/*-posix/`) para junto do `UniEmu.exe`.

## 4. Montar o pacote final

```
UniEmu\
├── UniEmu.exe                 ← build/UniEmu.exe
├── libwinpthread-1.dll        ← do MinGW (se os núcleos precisarem)
├── gamecontrollerdb.txt       ← third_party/gamecontrollerdb.txt
├── fonts\                     ← third_party/fonts/*
├── cores\                     ← os 7 núcleos acima
├── covers\_index\             ← dist/covers/_index/*
├── system\                    ← dist/system/*
├── emuladores\                ← dist/emuladores/*
├── roms\                      ← pasta vazia (opcional)
├── LEIA-ME.txt                ← dist/LEIA-ME.txt
└── LICENCAS.txt               ← dist/LICENCAS.txt
```

Compacte a pasta `UniEmu` como `UniEmu-<versão>-windows-x64.zip` e publique em *Releases*.

> **GPL:** ao distribuir os núcleos GPL compilados, informe onde está o código-fonte de cada um. A tabela
> com os repositórios e commits em `LICENCAS.txt` cumpre essa exigência.
