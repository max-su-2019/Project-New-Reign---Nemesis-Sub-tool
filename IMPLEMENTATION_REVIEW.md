# Nemesis Sub-tool 实现复查记录

检查日期为 2026-11-11。源码基线为 `51f058f`，本次将 `T2` 的 HKX Extractor 入口改为 Windows DLL。

## 接口与输出

`HKX_Extractor.dll` 导出 `GenerateBehaviorPatchFiles` 和 `GetBehaviorPatchLastError`。调用方可以通过公开头文件和导入库调用，也可以按导出名加载。生成函数的四个参数依次是 mod code、原始文件路径、修改后文件路径和补丁输出目录。mod code 接受 4 至 6 位 ASCII 字母或数字，并转为小写。路径采用 UTF-8，支持绝对路径以及相对宿主工作目录的路径。 [接口声明](T2/HKX_Extractor.h#L17) · [导出定义](T2/HKX_Extractor.def) · [调用示例](README.md#L17)

| 输入 | 处理与输出 |
| --- | --- |
| Behavior XML（`.xml`） | 直接读取 XML，建立并匹配两版行为图，将节点差异文件写到指定目录。 [行为图比较](T2/HKX_Extractor.cpp#L264) |
| Character XML（`.xml`） | 通过同一接口比较角色节点。修改 `hkbCharacterStringData.animationNames` 时，按节点 ID 输出补丁，并记录原版和编辑版的动画条目数。 [角色字符串比较](T2/src/hkx/hkbcharacterstringdata.cpp#L355) |
| `.hkx` | DLL 根据扩展名为每份输入调用 `hkxcmd.exe`，包括内容已经是 XML 的文件。转换器从 DLL 所在目录或宿主工作目录查找，转换结果写入本次调用的临时目录。 [转换调用](T2/HKX_Extractor.cpp#L189) |
| AnimData | 匹配动画项目和事件数据，在指定目录内按项目组织补丁。 [AnimData 处理](T2/AnimData/animdatacore.cpp#L15) |
| AnimSetData | 匹配动画集项目，在指定目录内按项目组织补丁。 [AnimSetData 处理](T2/AnimSetData/animsetdatacore.cpp#L15) |

比较成功返回 `true`，包括两份输入相同的情况。收到 `false` 后，可以在同一调用线程通过 `GetBehaviorPatchLastError()` 读取首条失败消息。DLL 拥有返回字符串，其有效期到该线程的下一次生成调用或线程退出。生成调用开始时清空上一条消息。 [API 实现](T2/HKX_Extractor.cpp#L398) · [错误查询](T2/HKX_Extractor.cpp#L504) · [详细用法](README.md#read-an-error)

DLL 实例串行处理生成调用，每次调用结束清理比较状态。补丁先写入本次调用的临时目录，比较成功后替换目标目录中的同名补丁。发布过程中发生错误时，已发布的文件会尝试回滚。 [状态清理](T2/Global.cpp#L122) · [补丁发布](T2/HKX_Extractor.cpp#L336)

## 构建与验证

根解决方案和 `T2.sln` 均加载同一个 DLL 项目。工程使用 MSVC `v143`、Windows SDK 10.0 和 Boost。可通过 MSBuild 属性 `BoostRoot` 与 `BoostLibraryDir` 指定库位置。 [根解决方案](Project%20New%20Reign%20-%20Nemesis.sln#L6) · [DLL 项目](T2/T2.vcxproj#L29) · [构建说明](README.md#L5)

Release x64 使用 MSVC 14.44 和 Boost 1.72 构建成功，产物为 `T2/x64/Release/HKX_Extractor.dll` 和 `T2/x64/Release/HKX_Extractor.lib`。本次构建将 `TEMP` 与 `TMP` 指向工作区内的可写目录，并传入 `/p:TrackFileAccess=false`。`dumpbin` 确认 DLL 为 x64 并按原名导出 `GenerateBehaviorPatchFiles`。

| 运行检查 | 结果 |
| --- | --- |
| Behavior XML | 用含中文和空格的输入及输出路径比较样本。补丁包含修改后的播放速度和 Nemesis 标记。 |
| 重复调用 | 在同一进程重复生成并替换同名补丁。相同输入返回成功且无新增补丁。短文件、损坏 XML、缺失文件及输出冲突返回失败。 |
| 多线程调用 | 三个线程调用同一 DLL 实例，在各自目录得到正确的补丁。 |
| 失败原因 | 验证空参数、非法 mod code、缺失文件和转换器、输入类型不匹配、未注册节点和输出冲突的错误消息。两个调用线程分别读取各自的错误，成功调用后读到空字符串。 |
| AnimData 和 AnimSetData | 修改动画事件时间生成 1 个补丁。比较 `output.txt` 和 `output2.txt` 生成 3 个补丁文件。 |
| 转换器调用 | 用替身 `hkxcmd.exe` 验证 DLL 同目录查找、Unicode 路径传参、转换结果读取和临时文件清理。 |
| XML 格式的 `.hkx` | Python 将两份 Atronach Frost 样本直接传给 API。DLL 用实际的 `hkxcmd.exe` 逐份转换，生成 54 个补丁。 |
| 原生 C++ 调用 | 独立程序通过公开头文件与导入库调用 DLL，并用相对路径生成补丁。 |

## Character 补丁生成复查（2026-11-13）

角色数据加载时先读取字符串节点，再解析引用其属性表的节点。空的 `wordVariableValues` 初始化为空列表。比较变量值时按各版本的实际条目数写出 `numelements`。声明数量与读取数量不一致时，生成返回失败并提供错误信息。 [加载顺序](T2/src/FillFunction.cpp#L127) · [变量值解析](T2/src/hkx/hkbvariablevalueset.cpp#L29) · [变量值比较](T2/src/hkx/hkbvariablevalueset.cpp#L274)

| Release x64 检查 | 结果 |
| --- | --- |
| Draugr 角色文件 | 通过实际的 `hkxcmd.exe` 转换 XML 原版和二进制编辑版，成功生成 `#0024.txt`。动画条目由 216 增至 222。 |
| 空变量列表 | 验证空列表增删值、非空值修改、新增空列表节点、数量错误诊断，以及失败后在同一进程重新调用。 |
| Dragon Priest Behavior 回归 | 生成 70 个补丁，其中 69 个与原结果逐字节一致。`#0089.txt` 补上 `wordVariableValues` 从 70 增至 75 的计数变化。 |
| Character 批量排查 | 递归测试 `BehaviorProjectTemp/originals` 下所有 `characters` 目录内的 46 个 Havok XML 格式 `.hkx`。每份编辑副本追加 3 条 `animationNames` 记录，使用 mod code `chra`。46 个文件均通过，共生成 46 个补丁，问题文件为 0。逐个验证了补丁两个分支与对应 XML 节点一致，原始文件的 SHA-256 一致。 |

这轮验证覆盖 Release x64 的转换和补丁生成。Win32、Debug、Nemesis 合并及游戏内加载仍待验证。
