# Sifli Vendor Guide

## 编译配置

### sf32lb52_devkit_lcd

清除配置：

```bash
rm -f nuttx/.config nuttx/.config.old nuttx/.version && rm -rf cmake_out/sf32lb52_devkit_lcd
```

配置编译：

```bash
cmake -B cmake_out/sf32lb52_devkit_lcd -S $PWD/nuttx -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"
```

编译：

```bash
cmake --build cmake_out/sf32lb52_devkit_lcd
```
