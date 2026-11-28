// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
struct apusys_core_info;
int apu_power_init(void);
void apu_power_exit(void);
int apupw_dbg_init(struct apusys_core_info *info);
void apupw_dbg_exit(void);

int apu_power_drv_init(struct apusys_core_info *info)
{
	return apupw_dbg_init(info);
}
EXPORT_SYMBOL(apu_power_drv_init);
void apu_power_drv_exit(void)
{
	apupw_dbg_exit();
}
EXPORT_SYMBOL(apu_power_drv_exit);
static int __init cannon_apu_init(void)
{
	return apu_power_init();
}
static void __exit cannon_apu_exit(void)
{
	apu_power_exit();
}
module_init(cannon_apu_init);
module_exit(cannon_apu_exit);
MODULE_DESCRIPTION("Cannon native APU 2.5 power backend");
MODULE_LICENSE("GPL");
