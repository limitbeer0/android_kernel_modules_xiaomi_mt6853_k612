# Cannon 诊断 boot 的 panic 自动回 Recovery

用户要求 panic 也自动进入 Recovery。本轮实测模式为 `WATCHDOG_TEST=android NATIVE_EXPDB=1 ADB_ROOT=1`，仍使用原厂 LK、独立 DTBO 和 Recovery 分区。

## 已验证路径与硬件辅助标记

1. ramdisk PID1 在 `ufs-mt6853.ko` 加载后立即等待 misc (`sdc2`)，只改 BCB 的前32字节 command 为 `boot-recovery`，执行 fsync 并回读比较，再继续 TEE/图形模块。后续 panic 或有限 WDT 复位都可由 LK 选择 Recovery。之前直到全部 ko 加载完才写，test52 在 CMDQ init 提前 panic，暴露了这个空窗。
2. `CONFIG_CANNON_BRINGUP` 的 MTK watchdog 在明确指定 `mtk_wdt.cannon_panic_recovery=1` 且 machine compatible 为 `xiaomi,cannon` 时注册高优先级 atomic panic notifier。它按正常4.14的 reboot("recovery") 实现，把 WDT `NONRST_REG2`（偏移0x24）的 bit1 置位并读回。panic 上下文只操作已映射 MMIO，不写 UFS、不分配、不等待存储锁。`panic=10` 之后的复位由原厂启动链处理。

`pack_boot.py` 为每个显式 watchdog/crypto 诊断模式加入该参数，普通封装不加入。**当前官方启动链只验证了 BCB 路径有效。** test54 清零并回读 BCB 后触发 sysrq-c，native expdb确认 panic notifier 已把 NONRST_REG2 置为0x40002002，并打印10秒后复位，但随后回到6.12 normal adb，未选择 Recovery。因此该硬件位目前只能作为辅助请求，不能代替 BCB，也不能声称覆盖 UFS 可用之前的 panic。未进入 panic 的硬锁死同样依赖有限硬件 WDT 和已提前写好的 BCB。

## 实机验证

test53 已实刷：3.143353秒完成 BCB 写入、fsync、回读；3.285046秒在 CMDQ 测试清理路径 panic，3.285167秒读回 NONRST_REG2=0x40002002，随后打印 Rebooting in 10 seconds。电脑08:25:40执行普通 reboot，08:26:32.883观察到 OrangeFox Recovery root 重连，主机未发出 reboot recovery 或后续引导指令。已实际验证 panic 自动回 Recovery。该次两条标记同时存在；test54的后续隔离测试已证明仅这个硬件标记不够。

证据在 ../../device-cannon/test53/，包括 console-ramoops、native/direct expdb、带时戳的 usb-observation.jsonl、镜像审计和 result.json。test54还验证了GCE scratch PASS，6.12 root adb可用；隔离测试证据在 ../../device-cannon/test54/decoded-isolation-expdb/。随后重新按默认BCB配置在113.875秒触发sysrq-c，打印10秒后复位，08:33:35.384电脑观察到Recovery root重连；未发送reboot recovery。pstore和native expdb均读回该次panic。证据在 ../../device-cannon/test54/default-panic/。因此UFS就绪之后的早期模块panic与Android运行后的panic均已实测自动回Recovery。

参考：正常4.14源码 `drivers/watchdog/mtk_wdt.c` 的 `mtk_reset_handler()` recovery 分支。原有作者和许可证保持。
