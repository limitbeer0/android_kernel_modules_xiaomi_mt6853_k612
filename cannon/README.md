# Cannon / MT6853T 6.12 源码移植

这是正在进行的 6.12 移植，尚未完成可用的 Android 系统。35个ko在ramdisk加载，
UFS枚举、TEE/RPMB/Keymaster及旧v1 fscrypt双向互读已实测；test44在旧Android16
激活36个APEX并推进至图形服务。完整UI和6.12用户CE解锁仍未验证。

2026-10-24用户要求切换官方MIUI V14.0.6.0.SJECNXM（旧Android16的HWC/SELinux被修改）。
已按用户指定flash_all.bat完成干净全量刷入并重启，等待首次设置和adb授权。
后续模板改为相邻device-cannon/miui14-official/boot.img，旧current-boot.img不能自动恢复。

当前验证结果与剩余缺口见 [STATUS.md](STATUS.md)。

## 基线和设备证据

- common：相邻 `android_kernel_xiaomi_mt6853_k612`，Linux 6.12.23。
- 设备模块：本仓库 `kernel_device_modules-6.12`，原有构建脚本针对 xaga/MT6895。
- 可启动参考：相邻 `MT6853T_xiaomi_custom_kernel` 的
  `arch/arm64/configs/cannon_defconfig` 与 `arch/arm64/boot/dts/mediatek/mt6853.dts`。
- 原始 DTS SHA256：`9c33273fdbce79f9665d9de489c580fbfa22053d55707b2548b991211db5db1b`。
  `cannon-stock.dtsi` 保留其原始内容，6.12 适配集中在 `cannon.dts`。
- 2026-10-18 adb 只读采集：运行内核 4.14.336，Android Clang r428724 / 13.0.1，
  GNU ld 2.24（Linaro 2017.01）；设备型号 M2007J22C，当时系统 Android 16。
  官方线刷包为 Android 12，因此两套用户空间不能视为相同环境。
- 实际 bootloader pstore mblock：`0x4d010000`，长度 `0xe0000`；console `0x40000`，
  pmsg `0x10000`。紧邻的 `0x4d0f0000` 是 minirdump，禁止挪作日志缓冲区。
- 官方 boot：Android header v2，page 2048；kernel `0x40080000`，ramdisk
  `0x47c80000`，tags/DTB `0x4bc80000`；DTB 槽是 MTK DT table。
  boot 分区 64 MiB；dtbo 独立，不在本轮替换。

## 已落实的源码

- `vendor/cannon.config`：GKI 底座 + 独立 cannon 片段，不合并大而全的 mgk/xaga 配置。
  Clang 13 初期关闭 CFI/LTO/BTF，保留 4K pages、compat、initrd、内置 pstore/串口/WDT。
  清空 GKI 附加的 ttynull/protected-KVM 参数，首阶段关闭未适配的 cpuidle/cpufreq，
  由 LK/DT 提供 bootargs，保持固件设置的频率。
- `CONFIG_CANNON_BRINGUP`：只进入已选择的 MT6853 clock/pinctrl 模块目录；
  禁用 xaga 固定物理地址诊断及无条件 MT6895 时钟模块。
- common 树两个 MediaTek Kconfig 菜单在 cannon 模式下关闭，防止 `ARCH_MEDIATEK`
  的默认选项同时带入上游与 OOT 两套时钟/Paris/EINT，造成重复导出符号。
- GPIO：将旧 `reg_bases` 转为 6.12 Paris 直接 MMIO `reg`；保持九个 bank 的
  地址与顺序。EINT 改为 `mediatek,eint`、128 路单实例、32 路硬件 debounce 的
  legacy 矩阵；IRQ 放到新驱动实际读取的 EINT 节点。
- 修复 Paris MMIO 分配/映射失败的 NULL 判断，避免错误路径继续解引用。
- 接入 MT6853 PWRAP、MT6359P MFD/IRQ/调压器三模块，保留原始 PMIC DTS 的节点
  和 regulator phandle；4.14 的 `mediatek,mt6359-pmic` 映射到 6.12 的 MT6359P cells。
  cannon 模式优先使用配套设备模块头文件，避免 common/设备树两套 PMIC 寄存器定义混用。
- common `fs/pstore/ram.c` 仅在 cannon 模式下保留第二个 console 大小的 AEE 空隙，
  并恢复 ftrace 在 pmsg 前面的顺序。record/ftrace 均 `0x1000`，console 起点
  `0x4d05f000`、pmsg 起点 `0x4d0e0000`，与 4.14 读端一致；test02 失败后已从 4.14 Recovery 读出完整 6.12 console。
- WDT 使用 common 驱动的通用寄存器 fallback，不虚构 MT6853 reset 编号表。
- `init.c`：Clang 13 编译的静态 AArch64 PID 1，无 libc/动态链接器/shell 依赖；
  通过 boot cmdline 的 `rdinit=/cannon-init` 选择，先加载
  `/lib/modules/modules.load`，之后保留 envp、将 argv[0] 还原为 `/init` 并 exec
  原始 `/init`。原始 init 文件保持不变。
  单个模块失败保留明确的 kmsg 错误并停住，不带残缺依赖集进入 Android。
- `stage_modules.py`：从当前 `modules.order` 取模块，递归解析 depends，拒绝缺失、循环、
  重名、版本不一致及未处理的 softdep；打包前 strip debug，记录 SHA256。
- `pack_boot.py`：以当前运行版本的 boot 为模板，保留 ramdisk 文件权限/所有者、
  boot 地址和 MTK DT table 身份字段；保留当前 boot 的 raw cpio 及 Image.gz 后追加
  raw DTB 的格式，替换两处 DTB，
  重算 boot ID，检查 64 MiB 限制及内核/模块 release。不会复用失效的原厂 AVB footer。

把驱动变成 ko **不会消除 probe、电源、时钟依赖**。这里解决的是 init 前加载和显式
依赖排序；硬件依赖仍需逐条适配。`clk_ignore_unused/pd_ignore_unused/fw_devlink=permissive`
只用于首阶段诊断，不作为后续完整系统的最终配置。

## 构建和打包

在 WSL/Linux、本仓库根目录执行：

```sh
bash build-cannon.sh config
bash build-cannon.sh kernel
bash build-cannon.sh modules
bash build-cannon.sh loader
bash build-cannon.sh dtb
python3 cannon/test_boot.py -v
python3 cannon/test_stage_modules.py -v
python3 cannon/verify_dt.py out-cannon/cannon.dtb ../device-cannon/current-dtbo.img
STOCK_DTBO=../device-cannon/current-dtbo.img bash cannon/package.sh \
  ../device-cannon/current-boot.img out-cannon/boot-cannon-test03.img
```

也可 `bash build-cannon.sh all`。脚本增量构建，不清理源码、不安装工具、不刷机。
默认工具链 `/usr/lib/llvm-13/bin` 为 Ubuntu Clang/LLD 13.0.1，**尚非原 r428724 完全复现**。
指定 `LLVM_BIN=/path/to/clang-r428724/bin` 可换成同一 AOSP clang 发行包。
本地 6.12 的 `scripts/min-tool-version.sh` 要求 LLVM >=13.0.1、binutils >=2.25，
因此不能直接承诺沿用 4.14 的 GNU ld 2.24；默认使用 LLD 13，不绕过版本检查。

可只读提取官方启动文件：

```sh
python3 cannon/inspect_stock.py ../cannon_images_V14.0.6.0.SJECNXM_20231017.0000.00_12.0_cn_ce9fc0ac48.tar ../stock-cannon
```

输出目录 `out-cannon/` 不入 Git。原厂镜像与 adb 采集位于上一级的
`stock-cannon/` 和 `device-cannon/`，包含设备身份/bootargs，不提交这些采集。

## 后续硬缺口

1. MT6359P/PMIC wrapper/调压器已完成源码接线与模块构建，仍待实际 probe 验证。
   UFS 已新增独立 `ufs-mt6853` variant，test04 已实测 HS-G3 双 lane 链路与 60 个分区；
   采用旧固件 0x276 reset/refclk、0x270 restore，保持固件初始 M-PHY 配置，
   禁用动态 PM/inline crypto，HS 最高 G3。
   真机 boot 在 `/dev/block/sdc41`，仅编译通用 UFS 并不代表 MT6853 存储可用。
2. 旧 M4U/ION ABI 与 6.12 IOMMU/dma-buf、LCM/fbdev 与 DRM、GPU 用户态接口。
3. 实机面板 `dsi_vdo_j22_42_02_0a_lcm_drv`，不能套用 xaga L16 面板。
4. 充电/触控/音频/相机/连接性、CCCI/TEE 与原厂固件 ABI。
5. 当前 Android 16 系统的 init/fstab 与 Android 12 原厂 ramdisk 差异。原厂 dtbo index 0
   已可与新 DTS 合并，并核对 GPIO/EINT/WDT/日志参数；其余外设绑定仍需逐项移植。
6. 早期 pstore、模块加载、GPIO/EINT/PWRAP/PMIC probe、first-stage init 已实测；
   first-stage mount 和完整 Android 仍待接通。

源码编译、DTB 生成和镜像格式测试都不能替代以上实机验证。当前首阶段镜像即使生成，
也不能标为“可开机”或“可日用”。

## test02 实机发现与修复

- bootloader 不支持 fastboot 临时启动；后续仅刷入 boot 测试。
- 以运行中的 Android 16 boot 为模板，保留原始 init/raw cpio/追加 DTB。
- 6.12 在约 3.09 秒进入原始 Android init；约 13.29 秒因无 UFS 分区、init 退出而 panic。
- 4.14 Recovery 能读出 193,066 字节的 6.12 console；已恢复原 boot 并核验 SHA256。
- 4.14 与 6.12 的 TOP/INFRA 时钟 ID 不同。cannon 模式在 provider 的 DT 查询处转换
  旧编号，内部注册仍用新编号；缺失旧 clock 返回 ENOENT，禁止误开其他时钟。
  表由 `cannon/gen_clock_map.py` 根据两套头文件和已核对寄存器的少量别名生成。
- 旧 regulator 节点使用下划线；通过 `regulator-compatible` 映射到 6.12 的连字符名称，
  保留原 phandle 和电压约束，使 UFS 的 vcc-supply 能找到实际 regulator。

测试打包会移除 ramdisk fstab 的 `formattable/check/resize` 标记；不允许适配失败自动触发格式化、修复或扩容。

## 电源键保留日志（用户 MTK_DEBUG.patch 的 6.12 适配）

原补丁为 MT6893 的旧 kpd/hal_kpd 路径，两棵 6.12 树均不能直接 apply。
本移植将相同思路放到实际使用的 `mtk-pmic-keys.c`：

- `CONFIG_CANNON_POWERKEY_PANIC=y` 且 `powerkey_panic=1` 时，只有按下 KEY_POWER
  才会触发 panic，释放事件和其他按键保持原行为。配置默认关闭，仅 cannon 调试配置启用。
- 可通过 `/sys/module/mtk_pmic_keys/parameters/powerkey_panic` 写入 0 关闭触发；这是调试功能，
  不能作为正常开机镜像的最终默认设置。
- PMIC keys 模块位于 UFS 前；通过 MFD 的 IRQ 48/50 接收电源按下/释放事件。
- cannon 模式跳过新驱动的 long-press reset 重配置，保留 LK 的硬复位救援路径。
- PMIC/PWRAP 中断必须能工作；全核或总线锁死不能依赖按键 panic。
- `WATCHDOG_TEST=1 bash cannon/package.sh ...` 额外加入诊断标记，PID 1 在加载
  模块前开启 20 秒硬件 WDT，并关闭 IRQ pretimeout。每个 ko 加载完成后喂狗；
  卡住则等待硬件复位。该诊断模式不 exec Android init，等待异步 SCSI 扫描和
  电源短按 panic/20 秒 WDT 复位，不挂载 Android 文件系统；默认打包不启用。

`WATCHDOG_TEST=android` 则保持 20 秒 WDT 并继续 exec 原 Android init，
用于挂载阶段取日志；WDT 默认打包为 0，不应把诊断镜像当作正常日用内核。
