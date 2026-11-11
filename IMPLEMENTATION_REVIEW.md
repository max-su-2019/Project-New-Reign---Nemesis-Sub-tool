# Nemesis Sub-tool 实现复查记录

检查日期为 2026-11-11。源码基线为 `51f058f`，本次将 `T2` 的 HKX Extractor 入口改为 Windows DLL。

## 接口与输出

`HKX_Extractor.dll` 导出 `GenerateBehaviorPatchFiles`。调用方可以通过公开头文件和导入库调用，也可以按导出名加载。四个参数依次是 mod code、原始文件路径、修改后文件路径和补丁输出目录。mod code 接受 4 至 6 位 ASCII 字母或数字，并转为小写。路径采用 UTF-8，支持绝对路径以及相对宿主工作目录的路径。 [接口声明](T2/HKX_Extractor.h#L17) · [导出定义](T2/HKX_Extractor.def) · [调用示例](README.md#L17)

| 输入 | 处理与输出 |
| --- | --- |
| Behavior XML | 建立并匹配两版行为图，将节点差异文件写到指定目录。 [行为图比较](T2/HKX_Extractor.cpp#L234) |
| 二进制 HKX | 从 DLL 所在目录或宿主工作目录查找 `hkxcmd.exe`，在本次调用的临时目录接收 XML 转换结果，再生成 Behavior 补丁。 [转换调用](T2/HKX_Extractor.cpp#L163) |
| AnimData | 匹配动画项目和事件数据，在指定目录内按项目组织补丁。 [AnimData 处理](T2/AnimData/animdatacore.cpp#L15) |
| AnimSetData | 匹配动画集项目，在指定目录内按项目组织补丁。 [AnimSetData 处理](T2/AnimSetData/animsetdatacore.cpp#L15) |

比较成功返回 `true`，包括两份输入相同的情况。参数、读取、比较或写入失败时返回 `false`。DLL 实例串行处理调用，每次调用结束清理比较状态。补丁先写入本次调用的临时目录，比较成功后替换目标目录中的同名补丁。发布过程中发生错误时，已发布的文件会尝试回滚。 [API 实现](T2/HKX_Extractor.cpp#L368) · [状态清理](T2/Global.cpp#L92) · [补丁发布](T2/HKX_Extractor.cpp#L306)

## 构建与验证

根解决方案和 `T2.sln` 均加载同一个 DLL 项目。工程使用 MSVC `v143`、Windows SDK 10.0 和 Boost。可通过 MSBuild 属性 `BoostRoot` 与 `BoostLibraryDir` 指定库位置。 [根解决方案](Project%20New%20Reign%20-%20Nemesis.sln#L6) · [DLL 项目](T2/T2.vcxproj#L29) · [构建说明](README.md#L5)

Release x64 使用 MSVC 14.44 和 Boost 1.72 构建成功，产物为 `T2/x64/Release/HKX_Extractor.dll` 和 `T2/x64/Release/HKX_Extractor.lib`。本次构建将 `TEMP` 与 `TMP` 指向工作区内的可写目录，并传入 `/p:TrackFileAccess=false`。`dumpbin` 确认 DLL 为 x64 并按原名导出 `GenerateBehaviorPatchFiles`。

| 运行检查 | 结果 |
| --- | --- |
| Behavior XML | 用含中文和空格的输入及输出路径比较样本。补丁包含修改后的播放速度和 Nemesis 标记。 |
| 重复调用 | 在同一进程重复生成并替换同名补丁。相同输入返回成功且无新增补丁。短文件、损坏 XML、缺失文件及输出冲突返回失败。 |
| 多线程调用 | 三个线程调用同一 DLL 实例，在各自目录得到正确的补丁。 |
| AnimData 和 AnimSetData | 修改动画事件时间生成 1 个补丁。比较 `output.txt` 和 `output2.txt` 生成 3 个补丁文件。 |
| 转换器调用 | 用替身 `hkxcmd.exe` 验证 DLL 同目录查找、Unicode 路径传参、转换结果读取和临时文件清理。 |
| 原生 C++ 调用 | 独立程序通过公开头文件与导入库调用 DLL，并用相对路径生成补丁。 |

真实二进制 HKX 转换需要用实际的 `hkxcmd.exe` 和游戏样本复查。Win32 与 Debug 配置仍待构建验证。
