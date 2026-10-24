# cannon 调试日志：expdb 原生保存与8MiB直接写入

2026-10-24 test47 已验证两条路径均能跨 WDT 重启读回。直接路径实际读回
1,301,308字节，CRC有效，146次提交；原生路径保存524,288字节的缓冲，
包含最早128KiB和最后393,152字节，中间覆盖长度在解码结果中明确标注。
8MiB是直接路径的循环容量，test47尚未产生8MiB日志；完整环绕的解码已做主机测试。

## 启用与构建

common 内置 `kernel/printk/cannon_log_store.c` 和 `cannon_expdb.c`，仅
CONFIG_CANNON_BRINGUP编译，均需明确命令行开启；正常封装默认关闭。

```bash
cd /home/lei/cannon/K612/android_kernel_modules_xiaomi_mt6853_k612
bash build-cannon.sh kernel
bash build-cannon.sh modules
bash build-cannon.sh loader
bash build-cannon.sh dtb
WATCHDOG_TEST=android NATIVE_EXPDB=1 \
STOCK_DTBO=../device-cannon/miui14-working/dtbo.img \
bash cannon/package.sh ../device-cannon/miui14-working/boot.img out-cannon/NEW-NAME.img
```

NATIVE_EXPDB=1目前加入 `cannon.log_store=1 cannon.expdb=1 cannon.expdb_seconds=660 mtk_wdt.cannon_budget=600`。
当前打包器要求配合A模式诊断WDT；它不会自动刷机，且拒绝覆盖已存在镜像。
当前普通系统无su；Recovery有root。boot恢复必须用MIUI14 working完整备份，
不能恢复旧Android16整份boot。

## 两条保存路径

- 原生：校验 `/chosen/log_store` 小端结构及SRAM头，保留PL/LK DRAM头。
  在SRAM 0x11df00+0x24/+0x28发布私有512KiB缓冲物理地址/长度，并设置原生保存标志。
  每条打印只清理所写缓存行，NMI/递归争锁时丢弃而不等待。
  下次Preloader复制到expdb末尾2MiB固件循环日志区。
- 直接：内存中8MiB文本环，独立worker等待UFS分区出现；按GPT分区名expdb、UUID
  `3b9e343b-cdc8-4d7f-9fa6-b6812e50ab62`、分区号4、40MiB大小共同确认目标。
  只写expdb偏移24MiB起的4KiB头和随后8MiB文本环，不占末尾2MiB。
  每批最多64KiB，以REQ_FUA写文本后提交CRC头；捕获打印路径不发I/O。
  writer默认最多运行120秒，可由cannon.expdb_seconds设置为120–660秒；当前诊断包用660秒。
  I/O错误停止直接通道并保留原生通道。

文本有内核时钟时间戳，单次格式化最多511字节（含时间戳）。元数据记录丢弃数量。
这是开发期日志通道，不是可在任意panic/硬锁死中同步写UFS的保证；卡死时以最后一次
成功提交为准，原生路径补充缓存已清理的末尾。

## Recovery读取与解码

通过已确认的设备serial读取整个expdb到主机，再执行：

```bash
python3 cannon/read_expdb.py /path/to/expdb.img /path/to/decoded
python3 cannon/test_expdb.py -v
```

读回格式：

- `CNL6LOG1`：64字节小端头、128KiB冻结前段、循环末尾；解码器处理固件窗口环绕，
  通过最后配置块魔数判断4096/512字节固件块大小，并标出丢失的中段。
- `CNL6DIS1`：4KiB提交头，头前60字节CRC32；cookie区分启动轮次。
  committed_bytes控制8MiB环的有效范围，produced_bytes表示该批捕获进度。
  解码器拒绝CRC不符、越界、截断或无效版本，输出原始文本字节与captures.json。

test47日志与原始分区镜像在 `../device-cannon/test47/`；原生及直接两份文本在
`decoded-expdb/`。其WDT仍在约33秒复位：当HW_RUNNING已置位时，watchdog core打开设备
直接ping并绕过start，导致首版有限喂狗没启动；test48已将启动点补到set_timeout，
实测三次喂狗且日志到93秒。用户要求延长后test49使用600秒预算，adb采样到605.98秒，
实际保存3,272,118字节直接日志和13,353,130字节连续logcat。test50进一步实测adb root与dmesg。
