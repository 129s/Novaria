# Novaria 构建说明（Windows）

## 当前状态

项目已在本地安装 SDL3 开发包到 `third_party/SDL3-3.2.0`。  
`CMakeLists.txt` 会优先使用该目录中的 `SDL3Config.cmake`，无需联网拉取。

## 前置条件

- CMake 3.24+
- Visual Studio 2022（含 C++ 工具链）

## 配置与编译

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

## 运行

```powershell
.\build\Debug\novaria.exe
```

可传入配置文件路径：

```powershell
.\build\Debug\novaria.exe config/game.toml
```

## 备选方案

如果你希望使用系统级 SDL3（而不是项目内置目录），可手动指定：

```powershell
cmake -S . -B build -DSDL3_DIR="你的SDL3Config.cmake所在目录"
```

如网络条件允许，也可启用自动拉取：

```powershell
cmake -S . -B build -DNOVARIA_FETCH_SDL3=ON
```
