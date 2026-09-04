# Next Bell Countdown

Windows 10/11 的透明、置顶、鼠标穿透倒计时浮窗。它使用逐像素 Alpha 合成的灰阶抗锯齿，背景完全透明且文字边缘不会出现 ClearType 彩边。它显示距离当天下一项时间点的剩余时间（精确显示到毫秒）；过了最后一项后会自动倒计时至翌日 08:50 AM。

时间表：08:50 AM、10:05 AM、10:15 AM、11:30 AM、12:30 PM、01:45 PM、01:55 PM、03:10 PM。

## 自定义

编辑 [NextBellCountdownConfig.h](NextBellCountdownConfig.h)，然后重新构建：

- `kSchedule`：增删或修改每天的时间点。`hour` 使用 24 小时制，`label` 是浮窗显示的名称。
- `kTextColor`：文字 RGB 颜色（每个值为 0–255）。
- `kEnableTextShadow`：设为 `false` 可关闭阴影；`kShadowOffsetX`、`kShadowOffsetY` 和 `kShadowOpacity` 分别控制方向、偏移及不透明度。

## 编译

在安装了 Visual Studio C++ 工作负载和 CMake 的 Developer PowerShell 中运行：

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\NextBellCountdown.exe
```

浮窗固定在主显示器工作区右下角，始终置顶且不拦截鼠标点击。按 `Ctrl + Alt + Q` 退出。

## 直接使用 MSVC

```powershell
cl /std:c++17 /EHsc /DWIN32_LEAN_AND_MEAN /Fe:NextBellCountdown.exe NextBellCountdown.cpp /link /SUBSYSTEM:WINDOWS
```
