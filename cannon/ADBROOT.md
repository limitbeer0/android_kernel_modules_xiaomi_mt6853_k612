# cannon 调试内核的 adb root

2026-10-24 test50 已实机验证：`adb root`返回已经是root，`adb shell id`为
uid0/gid0、`u:r:su:s0`，全局SELinux为Enforcing；`dmesg`和连续内核日志读取成功。
系统属性仍为ro.adb.secure=1、ro.debuggable=0，平台/vendor策略文件哈希与官方MIUI14一致。

## 实现

参考 tiann/KernelSU 的 adb_root 功能：

- [kernel/feature/adb_root.c](https://github.com/tiann/KernelSU/blob/main/kernel/feature/adb_root.c)
- [userspace preload](https://github.com/tiann/KernelSU/blob/main/manager/app/src/main/cpp/adbroot.cc)
- [SELinux credential transition](https://github.com/tiann/KernelSU/blob/main/kernel/selinux/selinux.c)

这里按当前6.12源码接入exec路径，不使用syscall探针或额外管理器：

1. 仅CONFIG_CANNON_BRINGUP、cannon设备及明确`cannon.adbroot=1`时生效。
2. 仅选择PID1启动、调用者uid0、root所有且不可被组/其他用户改写的adbd可执行文件。
3. 加载官方策略时，复用已有su类型，授权r角色并将**su调试域**设为permissive；
   系统其他域仍按原Enforcing规则检查。不是将整个SELinux设为permissive。
4. 在最终/dev tmpfs写入3840字节Bionic预加载库，随后切换并固定su exec SID，
   通过内核copy_string_kernel追加一个LD_PRELOAD环境变量，原argv/env保持顺序与计数。
5. 库只在adbd内部使debuggable查询和service.adb.root为真，并保留内核选定的su上下文。
   构造器清除LD_PRELOAD，子命令不再继续预加载；USB认证逻辑与ro.adb.secure保持原配置。

预加载库为`cannon/adbroot.c`，按上游GPLv3提供，见`adbroot-GPL-3.txt`。
它是独立的Bionic共享库；构建过程的libc/libdl链接桩不部署到设备。
内核侧cannon_adbroot.c为本机诊断适配，未加入完整KernelSU管理器/模块/allowlist体系。

## 构建与打包

```bash
cd /home/lei/cannon/K612/android_kernel_modules_xiaomi_mt6853_k612
bash build-cannon.sh kernel
bash build-cannon.sh modules
bash build-cannon.sh loader
bash build-cannon.sh dtb
WATCHDOG_TEST=android NATIVE_EXPDB=1 ADB_ROOT=1 \
STOCK_DTBO=../device-cannon/miui14-working/dtbo.img \
bash cannon/package.sh ../device-cannon/miui14-working/boot.img out-cannon/NEW-NAME.img
```

kernel/all目标会先用Clang/LLD13构建库并更新嵌入头。
单独构建库：`bash build-cannon.sh adbroot`。
pack_boot.py要求显式ADB_ROOT=1、原生expdb诊断与WDT模式，并检查内核中确有bootstrap标记。
不带ADB_ROOT的封装默认关闭；调试启动的adbd默认root，关闭需移除该启动参数重新封装。

## 使用与证据

```text
C:\adb\adb.exe -s pf6phuz565c685ws root
C:\adb\adb.exe -s pf6phuz565c685ws shell id
C:\adb\adb.exe -s pf6phuz565c685ws shell dmesg -w
```

test50镜像：`out-cannon/boot-cannon-test50-adbroot.img`，SHA256
`fafc5b5cac7fbae7cf411ea6e6f105527846c1d82141d83e00abd1ac046ac7a4`。
以MIUI14 working boot为模板，600秒诊断WDT、660秒expdb writer、37个ko；独立DTBO未刷写。
正常系统完整启动/CE尚未通过；当前仍缺DRM/Mali/ION等设备接口。

证据在`../device-cannon/test50/`：root-state-before/after、root-verification、
kmsg-live、dmesg-follow-verified及root-hardware-state。已用唯一标记写入/dev/kmsg，
并在实时流中读回；官方HWC哈希再次匹配，实际崩溃栈为DrmModeCrtc::getPlaneNum。

构建记录说明：首轮kernel构建已完成最终Image.gz，但运行中更新构建脚本导致其退出前
尾部读取出现EOF；当前脚本bash -n通过，模块与14项封装测试均通过，成品已实际刷机验证。

测试收尾：test50已自动进入Recovery，直接expdb读回3,559,144字节、CRC有效、1050次提交，
日志到606.53秒。已恢复MIUI14 working完整boot并读回SHA256 8bf3c216…eabb；
正常4.14.336、MIUI14.0.6、boot_completed=1、CE=true、Enforcing已确认。
