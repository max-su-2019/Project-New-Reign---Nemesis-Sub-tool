# Nemesis Sub-tool 实现复查记录

检查日期为 2026-11-11。记录基于 `master` 的 `2ef271b` 及当前工作区的命令行工程清理与构建配置变更。

## 工程结构

当前唯一的应用项目是 `T2` 下的 Windows C++ 命令行工具 HKX Extractor。Qt 和 C++/CLI WinForms 界面工程、资源、生成源码及随仓库附带的 Qt DLL 已移除。根解决方案和 `T2.sln` 均指向同一个命令行项目。 [根解决方案](Project%20New%20Reign%20-%20Nemesis.sln#L6) · [T2.sln](T2/T2.sln#L6) · [命令行入口](T2/main.cpp#L680)

## 输入与输出

命令行接收三个参数 `<modcode> <原始文件> <修改后文件>`。其他参数数量会进入交互模式，从当前工作目录读取 `output.txt` 和 `output2.txt`，并提示输入 `modcode` 和是否启用调试。仓库内的这对默认输入是 AnimSetData 样本。 [命令行入口](T2/main.cpp#L680) · [交互模式](T2/main.cpp#L486) · [默认原始文件](T2/output.txt) · [默认修改后文件](T2/output2.txt) · [运行说明](README.md#L17)

| 输入类型 | 处理流程 | 输出路径 |
| --- | --- | --- |
| Behavior | 读取 Havok XML，按注册类型创建节点，从根节点建立引用，匹配两版行为图并生成已有节点的差异或新增节点。 [解析](T2/src/FillFunction.cpp#L12) · [节点匹配](T2/main.cpp#L317) | `mod/<modcode>/<原始文件名去掉扩展名>/` |
| AnimData | 解析并匹配两版动画数据项目，再生成差异。 [AnimData 处理](T2/AnimData/animdatacore.cpp#L18) | `mod/<modcode>/animationdatasinglefile/` |
| AnimSetData | 解析并匹配两版动画集数据项目，再生成差异。 [AnimSetData 处理](T2/AnimSetData/animsetdatacore.cpp#L18) | `mod/<modcode>/animationsetdatasinglefile/` |

二进制 `.hkx` 需要在当前工作目录提供 `hkxcmd.exe`。读取器通过 Boost Process 启动转换器，并按独立参数传入输入和输出路径。 [转换分支](T2/Global-Addon.cpp#L202)

## 静态检查发现

| 问题 | 触发条件与影响 | 源码位置 |
| --- | --- | --- |
| 复用输出路径可能删除已有补丁 | Behavior 复用已存在的输出目录。若两份输入通过初步文本比较进入节点比较，且 `writtenFile` 最终为 0，程序会递归删除整个 Behavior 输出子目录，旧补丁也在删除范围内。 | [目录复用](T2/Global.cpp#L293) · [输出清理](T2/main.cpp#L635) |
| 处理失败可能返回成功退出码 | 读取失败等路径设置 `Error` 并返回。控制流回到 `main()` 后仍可能以退出码 `0` 结束，调用方会把失败判作成功。 | [读取失败](T2/main.cpp#L169) · [退出码](T2/main.cpp#L703) |
| 短输入可能越界 | 文件分类只检查行数组非空。数字开头的分支直接读取第 2 行，Behavior 探测分支最多读取前 4 行。 | [类型判断](T2/main.cpp#L68) |
| 相同输入的判断存在数据竞争 | 两个读取线程在计数同步后分别比较同一对字符串。任一线程判定相同后即可清空两份字符串并写入共享的 `same`，此时另一线程仍可能在比较。 | [读取同步与比较](T2/main.cpp#L40) |
| 字符串节点重复进入比较 | `hkbBehaviorGraphStringData` 和 `hkbCharacterStringData` 先单独比较。通用循环用 `\|\|` 连接两个类代码的排除条件，对非空节点恒真，导致这两类节点再次进入比较。 | [单独比较](T2/main.cpp#L335) · [通用循环](T2/main.cpp#L369) |
| 两个入口的 `modcode` 大小写规则不一致 | 交互输入转成小写，命令行直接使用参数值。补丁标记和输出目录使用各自入口处理后的值。 | [交互校验](T2/main.cpp#L433) · [命令行校验](T2/main.cpp#L680) · [补丁标记](T2/src/utilities/nemesisformat.cpp#L3) |

## 构建与验证

- `T2` 提供 Debug 和 Release 的 Win32、x64 配置，默认使用 Windows SDK 10.0 和 MSVC `v143`。Boost 默认从 `.deps/boost/` 查找，可通过 MSBuild 的 `BoostRoot` 和 `BoostLibraryDir` 指定其他安装路径。 [工具集](T2/T2.vcxproj#L21) · [Boost 配置](T2/T2.vcxproj#L56) · [构建说明](README.md#L5)
- 已使用 Visual Studio 2022、MSVC 14.44 和 Boost 1.72 完成 Release x64 构建。产物为 `T2/x64/Release/HKX Extractor.exe`。本次将 `TEMP`、`TMP` 指向工作区内可写目录，并传入 `/p:TrackFileAccess=false`，以绕开构建环境中的文件追踪权限错误。
- `dumpbin` 确认可执行文件使用 x64 Windows 控制台子系统，直接导入 Windows 系统 DLL 和 Microsoft Visual C++ 运行库。
- 37 个 Behavior XML 样本通过 XML 解析，声明的根节点均存在，样本中的 59 种对象类均已注册。 [对象类注册](T2/src/FillFunction.cpp#L156)

| 运行检查 | 结果 |
| --- | --- |
| Behavior XML | 修改样本中一个动画节点的播放速度，输出 1 个补丁，包含新值和 Nemesis 修改标记。 |
| AnimData | 修改样本中一个动画事件的时间，输出 1 个补丁，包含新值和 Nemesis 修改标记。 |
| AnimSetData | 比较仓库中的 `output.txt` 和 `output2.txt`，输出 3 个补丁文件。 |
| 转换器调用 | 在隔离目录用替身 `hkxcmd.exe` 验证带空格的路径参数、转换结果读取和临时文件清理，输出 1 个 Behavior 补丁。 |

真实二进制 HKX 转换仍需用实际的 `hkxcmd.exe` 和二进制样本验证。构建与运行检查覆盖 Release x64，Win32 和 Debug 配置仍待验证。
