# Cannon 6.12 源码状态（2026-10-18）

## 2026-10-24 晚间 test47–50：USB、长窗口及adb root已实测

当前手机已恢复MIUI14 working完整boot（8bf3c216…eabb），正常4.14.336、
boot_completed=1、CE=true、Enforcing、adb在线；本次正常adb实际为uid0/u:r:ksu:s0。
独立DTBO保持da975335…cbe8，LK/Preloader未修改或刷写。

- test48起6.12正常adb成功：修复MAC/IPPC资源重叠，并补齐旧PHY软件会话及IDDIG状态。
- test49/50实现600秒有限WDT窗口，expdb writer660秒；test50日志到606.53秒后自动Fox。
- expdb原生512KiB通道及直接8MiB循环区均实际保存；test50直接读回3,559,144字节、
  CRC有效、1050次提交、直接通道丢记录0。见[EXPDB.md](EXPDB.md)。
- test50参考tiann/KernelSU加入显式启动参数启用的adb root，实机uid0/u:r:su:s0，
  adb root及dmesg -w均通过；实时标记已回读。全局Enforcing，只有su调试域permissive，
  USB认证及ro.adb.secure=1保持。见[ADBROOT.md](ADBROOT.md)。
- Clang/LLD13，37个ko在boot ramdisk；MIUI14官方HWC与策略文件哈希均已由root核对。
- 完整UI/CE仍未通过。缺DRM/Mali/ION设备接口，官方HWC在
  DrmModeCrtc::getPlaneNum()+0崩溃，Mali EGL报kernel module not loaded。
  下一步应推进显示/DRM、Mali和内存分配兼容，不能把adb在线当作系统完整启动。
- device-cannon/test47–50各自保存result.json；test50已保存完整恢复验证。
  最新交接入口../../HANDOVER.md，以下段落为历史阶段记录。

## 2026-10-24 最新进度

**最新覆盖状态**：test45/46已刷入，均未取得6.12正常adb；已恢复MIUI14 working完整boot
`8bf3c2169c5eb77974068fdd286918454f7bfba207ecd01c436bf1d94c95eabb`，
4.14.336、boot_completed=1、CE=true、Enforcing、adb在线。恢复使用
`device-cannon/miui14-working/boot.img`，不再使用旧A16整份boot。
test46 pmsg保留了32KiB驱动快照，确认PHY成功，MTU3因MAC/IPPC重叠返回-EBUSY。
MAC大小已改为0x2e00，DTB重建、官方DTBO合并正例及重叠负例检查通过；修复尚未刷入。
90秒WDT helper尚未实机验证成功，test46仍约33秒复位。
最新用户要求优先分析LK扩大日志；用户只有官方LK，提供了不保证完全正确的参考PDF。
已核对证书与代码/DTB摘要、PL pstore分配/SRAM初始化及LK参数读取，详见
`../../device-cannon/lk-logging-20261024/REPORT.md`。
原生PL可通过SRAM klog接口保存内核日志到expdb，但每段裁为末尾512KiB。
尚无6.12 publisher或可刷的扩容LK；LK/Preloader及参考mainline未修改。

下列段落为此前的阶段记录：

MIUI14已确认正常启动。用户随后要求刷A16 boot；整份A16 ramdisk要求不存在的system_ext，
关闭AVB后仍循环重启。已保留A16 4.14内核/DTB并换回官方MIUI14原始ramdisk/header，
实测boot_completed=1、CE=true、Enforcing，4.14.336-perf-perf-g8dfe60978d49，adb在线。
新可启动恢复镜像位于device-cannon/miui14-a16-boot-20261024/boot-miui14-4.14-ksu.img。
test45新增common MTU3和T-PHY两个ko、USB2 peripheral DT适配、旧musb-hdrc UDC命名ABI，
6项模块staging测试及官方DTBO合并通过；内核构建中，尚未刷入。正常系统尚无su，Recovery有root。

用户要求切换为官方MIUI V14.0.6.0.SJECNXM测试基线，明确允许清空userdata并指定flash_all.bat。
已核对E盘包的34个文件及原脚本，flash_all.bat已退出0，全部分区写入及最终reboot均OKAY。
MIUI首次设置和USB调试授权已完成。后续6.12封装必须保留官方MIUI14 ramdisk，可以使用
device-cannon/miui14-official/boot.img；旧Android16 boot-original.img仅保留为历史备份。
用户指出旧系统HWC和SELinux有改动，因此test44图形故障不能作为官方MIUI表现的结论。

test39已实测修复RPMB旧协议和Keymaster TA加载，-65528反复崩溃消失；
vold安装系统DE密钥。仍有Keystore数据库、APEX文件内容读取异常，
完整Android启动和CE解锁尚未验证。原boot已刷回并核对哈希，dtbo未变；
用户已恢复USB调试，正常4.14已确认boot_completed=1、CE可用、Enforcing。
test41在禁止Android init、userdata只读+norecovery的条件下，对公开合成密钥测试文件
分别使用inlinecrypt fallback和软件fscrypt，两者均出现内容不匹配；恢复4.14后全部8192字节通过。
源码已定位4.14的v1普通文件使用原始主密钥和inode/block IV；test42兼容开关已实测生效，
inlinecrypt fallback和软件fscrypt均8192字节PASS。test43分别写入两个新文件，卸载后交叉读取通过；
恢复4.14后原文件和两个6.12文件也均8192字节PASS，CE/系统正常。
test44已激活36个APEX，Keystore earlyBootEnded和odsign原有key读取成功；
旧Android16推进到图形服务后崩溃，尚未验证6.12完整系统或CE解锁。已取日志并恢复旧boot。

最新完整记录：[PROGRESS-20261024.md](PROGRESS-20261024.md)。
下文是10月18日的历史状态，遇到差异以最新记录为准。

test05 已进入 Android second-stage init，挂载系统分区和 /data；
Beanpod Keymaster 4.1 初始化报 -65528。已用 WDT 热复位取回 console/pmsg，
恢复原 boot，4.14 正常启动且用户 CE 数据可用、SELinux Enforcing、dtbo 哈希未变。
当前模块数 33。最新 fs/exec.c 工具保护和 DT 参数尚未构建或验证。
完整交接：/home/lei/cannon/K612/HANDOVER.md。

## 已验证

- Ubuntu Clang/LLD 13.0.1 完整构建 Linux `6.12.23-cannon-bringup+`：
  `Image` / `Image.gz` / `vmlinux` / `System.map` / `Module.symvers` 均已生成。
- 外部模块严格 modpost 通过，无 undefined/重复导出警告；31 个 ko：
  25 个 MT6853 时钟模块、3 个 GPIO/EINT 模块，以及
  `mtk-pmic-wrap.ko`、`mt6397.ko`、`mt6359p-regulator.ko`。
- 静态 ARM64 PID 1 加载器编译通过，无动态链接器、无未解析符号。
- 原厂 DTBO index 0 与新 cannon DTB 可合并；合并后 GPIO 九个 bank 顺序、EINT
  128 路与 IRQ 212、PIO→EINT 引用、WDT fallback、ramoops 参数检查通过。
- boot v2/MTK DT table/raw cpio/追加 DTB/ramdisk 权限与 boot ID 的 8 项测试通过；模块依赖顺序、
  缺失依赖、循环、旧产物排除及版本拒错的 4 项测试通过。
- 31 个 ko 已经递归解析依赖并实际放入新 boot ramdisk 的 `/lib/modules`，通过
  `rdinit=/cannon-init` 在未修改的原 `/init` 前加载。test02 16,224,256 字节，
  以当前运行 Android 16 的 boot 为模板，保留其 raw cpio 和 raw appended DTB。
- 当前真机 boot、dtbo 只读备份已与设备侧 SHA256 对照一致，位于上一级
  `device-cannon/`；已执行 test02 刷入、失败日志提取和原 boot 恢复；没有刷写 dtbo/其他分区。

## 实机测试产物

- test02：`out-cannon/boot-cannon-test02.img`，
  SHA256 `9626ac7878be23a3dccf9e99242e67d823fd22c2469a599f4cfe9c8eba9331c8`。
- 原 boot 备份及恢复后分区 SHA256：
  `55454a88ce62f6380ebcd9243195ec0571c8651e03911581457daa1e19ddfc04`。
- 日志：上一级 `device-cannon/test02/console-ramoops`、
  `recovery-pstore.tar`、`recovery-dmesg.txt`、`restored-boot.txt`。
- 8 CPU 已启动；31 ko 加载完成；GPIO/EINT、PWRAP 写测试、PMIC MFD 和 regulator
  probe 成功。regulator 的 DT 名称匹配和 TOP/INFRA 旧编号转换根据日志继续修正。
- 约 3.09 秒进入 Android init，约 13.29 秒因没有存储分区触发 init panic。
  不代表 UFS、数据解密、显示、USB、完整系统可用。

## test03（失败测试已恢复原 boot）

- 新增 `ufs-mt6853.ko`，当前模块数 32；严格 modpost 通过，无未解析符号。
- TOP/INFRA provider 的 DT 查询兼容 4.14 编号；regulator 节点补充匹配名称。
- 新内核、DTB、32 ko 已打包；8 项 boot/安全 fstab 测试与 4 项模块依赖测试通过。
- 当前 DTBO 合并后 UFS compatible、reset syscon、旧 clock IDs 和 VCC phandle 检查通过。
- `out-cannon/boot-cannon-test03.img`，16,242,688 字节，
  SHA256 `f611fd9127149fa5c42bea8bb2ce6f9213dc19e1dce1f9249ff342deea4a498b`。
- 已仅通过 `fastboot flash boot` 刷入；用户观察到卡死，长按电源进入 Recovery，
  bootreason=`reboot_longkey`，pstore 为空；尚不能根据日志确定是否卡在 UFS。
- 已恢复原 boot，分区 SHA256 与备份一致；再次核对 dtbo 未变。
- test04 后续完成（见下节）：移植用户 MTK_DEBUG.patch 的短按电源 panic 思路到 PMIC keys，
  在 UFS 前加载；保留 LK 的长按硬复位配置，增加可选 20 秒 WDT 与 UFS 步骤日志。

## test04-debug（UFS 扫盘与 WDT 保留日志通过）

- 按用户 MTK_DEBUG.patch 思路适配 6.12 的 `mtk-pmic-keys.c`，配置默认关闭，
  cannon 调试配置启用。PMIC keys 作为 ko 排在 UFS 前，仅按下 KEY_POWER 触发 panic。
- 保留 LK 的长按硬复位配置；UFS 增加 platform/clock/HCE/link/SMC 步骤日志。
- 33 个 ko 严格 modpost 通过，未解析符号为 0；配置、DTBO 合并及 12 项测试通过。
- 诊断 PID 1 在加载模块前开启 20 秒 WDT，关闭 IRQ pretimeout，每个模块完成后喂狗；
  全部加载后等异步 SCSI 扫描，不进入 Android init，不挂载 Android 文件系统。
- `out-cannon/boot-cannon-test04-debug.img`，16,267,264 字节，
  SHA256 `1616be5d22f6a65a44ed86f6596a51201bc75115ea8719e8764ba1cc3bb4c401`。
- 仅刷入 boot，WDT 自动复位，Recovery bootreason=`HW_reboot`，
  取回 194,670 字节 console。原 boot 已恢复并核对 SHA256，dtbo 未变。
- 约 3.01 秒建立 HS-G3、双 lane、rate B 链路；识别
  SAMSUNG KM8V8001JM-B813 2302，128 GB/119 GiB、4096 字节逻辑块，
  枚举 `sdc1` 到 `sdc60`，基础分区表读盘成功。
- PMIC keys probe 成功，短按 panic 已注册；用户确认本轮等待自动重启，
  因此未验证实际按键触发。未挂载 Android 文件系统，不能据此宣称完整系统可启动。

## 日志兼容性

参考 4.14 `fs/pstore/ram.c` 与真机 `/proc/iomem`，6.12 cannon 模式保留：

| 区域 | 起点 | 大小 |
|---|---:|---:|
| dmesg，79 个 4 KiB record | `0x4d010000` | `0x4f000` |
| console | `0x4d05f000` | `0x40000` |
| 4.14 AEE 第二 console，6.12 不写入 | `0x4d09f000` | `0x40000` |
| ftrace | `0x4d0df000` | `0x1000` |
| pmsg | `0x4d0e0000` | `0x10000` |

两版本 persistent RAM header 的 sig/start/size 格式相同。
test02 已实测：4.14 Recovery 读出 193,066 字节的 6.12 console，包含从内核入口、
8 CPU 启动、模块加载到 first-stage init panic 的完整过程。

## UFS 存储链

已定位的适配依据：

- 4.14 不是 `ufs-mediatek.c`，实际实现是 `drivers/scsi/ufs/ufs-mtk.c` 与
  `drivers/scsi/ufs/mediatek/mt6853/ufs-mtk-platform.c`。
- 旧 DTS host compatible 是 `mediatek,ufshci`，6.12 设备模块匹配表仅有
  `mediatek,mt8183-ufshci`。不能单独添加 compatible 就宣布硬件支持。
- 4.14 generic UFS SMC FID 为 `0x276`，reset 使用 bit 1，参考时钟通知使用 bit 3；
  旧参考时钟在开启前/关闭后各通知一次，6.12 带 PRE/POST stage 语义。
- 4.14 crypto restore 使用另一个 FID `0x270` 的 bit 2，6.12 crypto 使用
  `0x276` 的 bit 2；该接口不能直接视为兼容。首阶段不得先假定 inline crypto 可用。
- 4.14 MT6853 M-PHY init 是空操作；6.12 的可选 PHY、MCQ、动态时钟与运行时电源
  路径仍需按旧硬件逐项取舍。新增 `ufs-mt6853.c`，按旧 HCI reset、UniPro 属性、SMC 和参考时钟流程接到
  6.12 ufshcd core，作为 ko 加入 ramdisk；动态 PM、MCQ、inline crypto 尚未启用。
  test04 实机链路建立和 60 分区枚举通过，Android 挂载已在 test05 部分验证，见末节。

test02 证实 PWRAP 寄存器写测试和基础 probe 成功；时钟编号和 regulator 名称修正、
UFS 链路与读分区表在 test04 通过；写盘、长期稳定性、休眠与加密仍待后续验证。

## 源码基线

- common：`20b279ed9c0bf75c52c5a3b8bf6c99a9cc11dd20` + 工作区修改。
- modules：`7c3d5677804b46fda6838537f95bf3ce2d5953ad` + 工作区修改。
- 4.14 参考：`f6f89c0eec98e1cea646aeeb49bd38988612fdf5`，未修改。
- 当前运行 4.14 `/proc/version` 的构建提交为 `8dfe60978d49`，与本地参考 HEAD 不同；
  因此编译器版本、实际 DT、pstore 和启动镜像均以 adb 只读采集作补充证据。

工具链仍是 Ubuntu 13.0.1；未把它等同于原 Android r428724 + Linaro GNU ld 2.24。

测试打包移除 ramdisk fstab 的 `formattable/check/resize` 标记，但 test05 证实 vendor fstab 仍触发 fsck/resize；后续保护状态见末节。

## test05-android（日志已提取，原 boot 已恢复）

沿用 test04 内核和33ko，加载后 exec 原 Android init，并保留20秒 WDT。
镜像16,267,264字节，SHA256：
`a4b84ae43c8a2a0b81975edf0cabee7188fd9cfd5c0c23d2bfacc8403f49b3e9`。

- 10.63s进入second-stage init，系统分区挂载；20.984s /data F2FS挂载，21s post-fs-data。
- pmsg反复报 Could not initialize BeanpodKeymaster41 (-65528)，服务SIGABRT。
  旧运行配置用TEEI400，新基线只有510/520；仅完成调查，尚未移植400。
- ramdisk fstab移除check/resize等标记未覆盖vendor fstab，仍执行fsck.f2fs和resize.f2fs；
  输出分别为 No error was reported 和 Nothing to resize。
- WDT热复位取回262132B console、16094B pmsg；未证明6.12用户CE解锁或完整开机。
- 恢复原boot并核对SHA256，dtbo未变；4.14 boot_completed=1、CE=true、Enforcing。
- 最新源码在CANNON模式增加exec工具保护，默认关闭，DT设cannon.block_fs_tools=1；
  fs/exec.c、cannon.dts、verify_dt.py已改，但尚未重建内核/DTB或实机验证。
  test05镜像不含保护，下轮Android测试前须先审查和验证。

详细交接：/home/lei/cannon/K612/HANDOVER.md。
