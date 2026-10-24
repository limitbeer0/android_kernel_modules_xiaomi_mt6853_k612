// SPDX-License-Identifier: GPL-2.0-only
/*
 * MT6853 first-stage UFS port.
 *
 * Register/SMC sequences come from the working 4.14 ufs-mtk.c and
 * mediatek/mt6853/ufs-mtk-platform.c (MediaTek, 2019).
 * Do not use the newer MT6895 PHY, MCQ or secure crypto ABI on this firmware.
 */
#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <ufs/ufshcd.h>
#include <ufs/ufs_quirks.h>
#include <ufs/unipro.h>

#include "ufshcd-pltfrm.h"

#if IS_ENABLED(CONFIG_MTK_RPMB)
#include "ufshcd-priv.h"
#include <linux/async.h>
#include <linux/rpmb.h>
#include <linux/semaphore.h>
#include <linux/unaligned.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#endif

#define MT6853_SIP_UFS_CTL	0xc2000276
#define MT6853_SIP_FDE_CTL	0xc2000270
#define MT6853_DEVICE_RESET	BIT(1)
#define MT6853_REFCLK_NOTIFY	BIT(3)
#define MT6853_FDE_RESTORE	BIT(2)
#define MT6853_HCI_RESET_SET	0x130
#define MT6853_HCI_RESET_CLR	0x134
#define MT6853_HCI_RESET		BIT(15)
#define MT6853_XOUFS		0x144
#define MT6853_XOUFS_REQ		BIT(0)
#define MT6853_XOUFS_ACK		BIT(1)
#define MT6853_DEBUG_CLK		0xd0a1
#define MT6853_SAVE_POWER	0xd0a6
#define MT6853_UNIPRO_PD		0xd0a8
#define MT6853_PA_SCRAMBLING	0x1585

struct mt6853_ufs_host {
	struct regmap *infra;
	u32 hs_gear;
};

static int mt6853_smc(struct ufs_hba *hba, u32 fid, u32 operation, u32 value)
{
	struct arm_smccc_res res;

	dev_info(hba->dev, "phase: SMC %#x op %#x value %u begin\n", fid, operation, value);
	arm_smccc_smc(fid, operation, value, 0, 0, 0, 0, 0, &res);
	dev_info(hba->dev, "phase: SMC return %#lx\n", res.a0);
	if ((int)res.a0 < 0) {
		dev_err(hba->dev, "SMC %#x op %#x value %u failed: %#lx\n",
			fid, operation, value, res.a0);
		return -EIO;
	}
	return 0;
}

static int mt6853_device_reset(struct ufs_hba *hba)
{
	int ret;

	ret = mt6853_smc(hba, MT6853_SIP_UFS_CTL, MT6853_DEVICE_RESET, 0);
	if (ret)
		return ret;
	usleep_range(10, 15);
	ret = mt6853_smc(hba, MT6853_SIP_UFS_CTL, MT6853_DEVICE_RESET, 1);
	if (ret)
		return ret;
	usleep_range(10000, 15000);
	dev_info(hba->dev, "MT6853 legacy device reset complete\n");
	return 0;
}

static int mt6853_setup_clocks(struct ufs_hba *hba, bool on,
			       enum ufs_notify_change_status stage)
{
	u16 vendor = hba->dev_info.wmanufacturerid;
	u32 val, delay;
	int ret;

	dev_info(hba->dev, "phase: clocks %s stage %u\n", on ? "on" : "off", stage);
	/* As in 4.14: boot ROM/LK already supplied the initial reference clock. */
	if (!vendor || (on && stage != POST_CHANGE) ||
	    (!on && stage != PRE_CHANGE))
		return 0;
	/* Never release the reference clock while the device link is active. */
	if (!on && !ufshcd_is_link_hibern8(hba))
		return 0;

	if (on) {
		ret = mt6853_smc(hba, MT6853_SIP_UFS_CTL,
				 MT6853_REFCLK_NOTIFY, 1);
		if (ret)
			return ret;
	} else {
		delay = vendor == UFS_VENDOR_TOSHIBA ? 100 :
			vendor == UFS_VENDOR_SAMSUNG ? 1 : 30;
		udelay(delay);
	}

	ufshcd_writel(hba, on ? MT6853_XOUFS_REQ : 0, MT6853_XOUFS);
	ret = readl_poll_timeout(hba->mmio_base + MT6853_XOUFS, val,
				on ? (val & MT6853_XOUFS_ACK) :
				     !(val & MT6853_XOUFS_ACK), 50, 2400);
	if (ret) {
		dev_err(hba->dev, "MT6853 reference clock %s ack failed: %#x\n",
			on ? "on" : "off", val);
		return ret;
	}
	if (on) {
		delay = vendor == UFS_VENDOR_TOSHIBA ||
			vendor == UFS_VENDOR_SAMSUNG ? 32 : 30;
		udelay(delay);
		return 0;
	}
	return mt6853_smc(hba, MT6853_SIP_UFS_CTL, MT6853_REFCLK_NOTIFY, 0);
}

static int mt6853_hce_notify(struct ufs_hba *hba,
			    enum ufs_notify_change_status stage)
{
	struct mt6853_ufs_host *host = ufshcd_get_variant(hba);
	int ret;

	dev_info(hba->dev, "phase: HCE stage %u\n", stage);
	if (stage == PRE_CHANGE) {
		hba->vps->hba_enable_delay_us = 600;
		/* Reset only HCI, matching the normal (non-LPM) 4.14 HCE path. */
		ret = regmap_write(host->infra, MT6853_HCI_RESET_SET,
				   MT6853_HCI_RESET);
		if (ret)
			return ret;
		udelay(100);
		ret = regmap_write(host->infra, MT6853_HCI_RESET_CLR,
				   MT6853_HCI_RESET);
		udelay(100);
		return ret;
	}
	/*
	 * 4.14 always restores the vendor crypto block after HCE. This is NOT
	 * the new 0x276 crypto call, and does not expose inline encryption.
	 */
	return mt6853_smc(hba, MT6853_SIP_FDE_CTL, MT6853_FDE_RESTORE, 0);
}

static int mt6853_update_attr(struct ufs_hba *hba, u16 attr, u32 clear, u32 set)
{
	u32 val;
	int ret;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(attr), &val);
	if (ret)
		return ret;
	return ufshcd_dme_set(hba, UIC_ARG_MIB(attr), (val & ~clear) | set);
}

static int mt6853_link_notify(struct ufs_hba *hba,
			     enum ufs_notify_change_status stage)
{
	u32 val;
	int ret;

	dev_info(hba->dev, "phase: link startup stage %u\n", stage);
	if (stage == PRE_CHANGE) {
		hba->ahit = 0;
		ufshcd_writel(hba, 0, REG_AUTO_HIBERNATE_IDLE_TIMER);
		ret = ufshcd_dme_get(hba, UIC_ARG_MIB(MT6853_UNIPRO_PD), &val);
		if (ret)
			return ret;
		if (val == 1) {
			ret = ufshcd_dme_set(hba, UIC_ARG_MIB(MT6853_UNIPRO_PD), 0);
			if (ret)
				return ret;
		}
		ret = mt6853_update_attr(hba, MT6853_SAVE_POWER, BIT(6), 0);
		if (ret)
			return ret;
		return ufshcd_dme_set(hba, UIC_ARG_MIB(MT6853_PA_SCRAMBLING), 0);
	}

	/* 4.14 treats local TX LCC disable failure as non-fatal. */
	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(PA_LOCAL_TX_LCC_ENABLE), 0);
	if (ret)
		dev_warn(hba->dev, "local TX LCC disable failed: %d\n", ret);
	ret = mt6853_update_attr(hba, MT6853_SAVE_POWER, 0,
				 BIT(0) | BIT(2) | BIT(3));
	if (ret)
		return ret;
	ret = mt6853_update_attr(hba, MT6853_DEBUG_CLK, BIT(5), 0);
	if (!ret)
		dev_info(hba->dev, "MT6853 link startup complete\n");
	return ret;
}

static int mt6853_pwr_notify(struct ufs_hba *hba,
			    enum ufs_notify_change_status stage,
			    struct ufs_pa_layer_attr *dev_max,
			    struct ufs_pa_layer_attr *agreed)
{
	static const u16 timeout_attr[] = {
		PA_PWRMODEUSERDATA0, PA_PWRMODEUSERDATA1, PA_PWRMODEUSERDATA2,
		PA_PWRMODEUSERDATA3, PA_PWRMODEUSERDATA4, PA_PWRMODEUSERDATA5,
	};
	static const u32 timeout_value[] = {
		0x1fff, 0xffff, 0x7fff, 0x1fff, 0xffff, 0x7fff,
	};
	struct mt6853_ufs_host *host = ufshcd_get_variant(hba);
	struct ufs_host_params params;
	int i, ret;

	if (stage != PRE_CHANGE)
		return 0;
	ufshcd_init_host_params(&params);
	params.hs_rx_gear = host->hs_gear;
	params.hs_tx_gear = host->hs_gear;
	params.rx_lanes = hba->lanes_per_direction;
	params.tx_lanes = hba->lanes_per_direction;
	ret = ufshcd_negotiate_pwr_params(&params, dev_max, agreed);
	if (ret)
		return ret;
	agreed->gear_rx = min(agreed->gear_rx, agreed->gear_tx);
	agreed->gear_tx = agreed->gear_rx;
	for (i = 0; i < ARRAY_SIZE(timeout_attr); i++) {
		ret = ufshcd_dme_set(hba, UIC_ARG_MIB(timeout_attr[i]),
				     timeout_value[i]);
		if (ret)
			return ret;
	}
	dev_info(hba->dev, "MT6853 power mode: gear %u lanes %u/%u rate %u\n",
		 agreed->gear_rx, agreed->lane_rx, agreed->lane_tx, agreed->hs_rate);
	return 0;
}

#if IS_ENABLED(CONFIG_MTK_RPMB)
/*
 * UFS RPMB support for the TEEI secure world.
 *
 * The secure world (uTOS) loads its trusted applications from RPMB, so
 * tee-supplicant needs a working /dev/rpmb0.  ufshcd already adds the RPMB
 * well-known LUN; this code locates it, publishes an rpmb_dev through the
 * MTK rpmb core module and routes RPMB frames with SECURITY PROTOCOL
 * IN/OUT commands, mirroring the 4.14 ufs-mtk.c behaviour.
 */
static struct semaphore mt6853_rpmb_sem;
static struct scsi_device *mt6853_rpmb_sdev;
static struct rpmb_dev *mt6853_rpmb_rawdev;

#define MTK_SEC_PROTOCOL_UFS		0xEC
#define MTK_SEC_SPECIFIC_UFS_RPMB	0x0001
#define MTK_SEC_PROTOCOL_CMD_SIZE	12
#define MTK_SEC_PROTOCOL_RETRIES	3

static int mt6853_rpmb_cmd_seq(struct device *dev, struct rpmb_cmd *cmds,
			       u32 ncmds, u8 region);

static struct rpmb_ops mt6853_rpmb_dev_ops = {
	.cmd_seq = mt6853_rpmb_cmd_seq,
	.type = RPMB_TYPE_UFS,
};

static int mt6853_rpmb_security_out(struct scsi_device *sdev,
				    struct rpmb_frame *frames, u32 cnt,
				    u8 region)
{
	struct scsi_sense_hdr sshdr = {0};
	u32 trans_len = cnt * sizeof(struct rpmb_frame);
	u8 cmd[MTK_SEC_PROTOCOL_CMD_SIZE];
	int ret;

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = SECURITY_PROTOCOL_OUT;
	cmd[1] = MTK_SEC_PROTOCOL_UFS;
	put_unaligned_be16((region << 8) | MTK_SEC_SPECIFIC_UFS_RPMB, cmd + 2);
	put_unaligned_be32(trans_len, cmd + 6);

	ret = scsi_execute_cmd(sdev, cmd, REQ_OP_DRV_OUT, frames, trans_len,
			       msecs_to_jiffies(30000), MTK_SEC_PROTOCOL_RETRIES,
			       &(struct scsi_exec_args) {
					.sshdr = &sshdr,
					.resid = NULL,
				});
	if (ret)
		dev_info(&sdev->sdev_gendev, "%s: failed with err %x\n",
			 __func__, ret);
	if (scsi_sense_valid(&sshdr) && sshdr.sense_key)
		scsi_print_sense_hdr(sdev, "rpmb: security out", &sshdr);
	return ret;
}

static int mt6853_rpmb_security_in(struct scsi_device *sdev,
				   struct rpmb_frame *frames, u32 cnt,
				   u8 region)
{
	struct scsi_sense_hdr sshdr = {0};
	u32 alloc_len = cnt * sizeof(struct rpmb_frame);
	u8 cmd[MTK_SEC_PROTOCOL_CMD_SIZE];
	int ret;

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = SECURITY_PROTOCOL_IN;
	cmd[1] = MTK_SEC_PROTOCOL_UFS;
	put_unaligned_be16((region << 8) | MTK_SEC_SPECIFIC_UFS_RPMB, cmd + 2);
	put_unaligned_be32(alloc_len, cmd + 6);

	ret = scsi_execute_cmd(sdev, cmd, REQ_OP_DRV_IN, frames, alloc_len,
			       msecs_to_jiffies(30000), MTK_SEC_PROTOCOL_RETRIES,
			       &(struct scsi_exec_args) {
					.sshdr = &sshdr,
					.resid = NULL,
				});
	if (ret)
		dev_info(&sdev->sdev_gendev, "%s: failed with err %x\n",
			 __func__, ret);
	if (scsi_sense_valid(&sshdr) && sshdr.sense_key)
		scsi_print_sense_hdr(sdev, "rpmb: security in", &sshdr);
	return ret;
}

static int mt6853_rpmb_cmd_seq(struct device *dev, struct rpmb_cmd *cmds,
			       u32 ncmds, u8 region)
{
	unsigned long flags;
	struct ufs_hba *hba;
	struct scsi_device *sdev;
	struct rpmb_cmd *cmd;
	int i, ret;

	hba = dev_get_drvdata(dev);
	if (!hba)
		return -ENODEV;

	spin_lock_irqsave(hba->host->host_lock, flags);
	sdev = mt6853_rpmb_sdev;
	if (sdev) {
		ret = scsi_device_get(sdev);
		if (!ret && !scsi_device_online(sdev)) {
			ret = -ENODEV;
			scsi_device_put(sdev);
		}
	} else {
		ret = -ENODEV;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	if (ret)
		return ret;

	down(&mt6853_rpmb_sem);
	scsi_autopm_get_device(sdev);
	for (ret = 0, i = 0; i < ncmds && !ret; i++) {
		cmd = &cmds[i];
		if (cmd->flags & RPMB_F_WRITE)
			ret = mt6853_rpmb_security_out(sdev, cmd->frames,
						       cmd->nframes, region);
		else
			ret = mt6853_rpmb_security_in(sdev, cmd->frames,
						      cmd->nframes, region);
	}
	scsi_autopm_put_device(sdev);
	up(&mt6853_rpmb_sem);

	scsi_device_put(sdev);
	return ret;
}

static void mt6853_rpmb_add(void *data, async_cookie_t cookie)
{
	struct ufs_hba *hba = data;
	struct scsi_device *sdev = NULL;
	struct device *dev = hba->dev;
	struct rpmb_dev *rdev;
	u8 *desc_buf;
	u8 rw_size = 1;
	int ret, i;

	/* Wait (up to ~10 s) for ufshcd to add the SCSI well-known LUNs. */
	for (i = 0; i < 100 && !mt6853_rpmb_sdev; i++) {
		shost_for_each_device(sdev, hba->host) {
			/* SCSI W-LUN id: (RPMB 0xc4 & ~UFS_WLUN_ID) | 0xc100. */
			if (sdev->lun ==
			    ((UFS_UPIU_RPMB_WLUN & ~UFS_UPIU_WLUN_ID) |
			     SCSI_W_LUN_BASE)) {
				mt6853_rpmb_sdev = sdev;
				scsi_device_put(sdev);
				goto found;
			}
		}
		msleep(100);
	}
	dev_info(dev, "mt6853-rpmb: scsi rpmb device not found\n");
	shost_for_each_device(sdev, hba->host)
		dev_info(dev, "mt6853-rpmb: scsi lun=0x%x\n", sdev->lun);
	return;

found:
	desc_buf = kmalloc(QUERY_DESC_MAX_SIZE, GFP_KERNEL);
	if (!desc_buf)
		return;

	ufshcd_rpm_get_sync(hba);
	ret = ufshcd_read_desc_param(hba, QUERY_DESC_IDN_GEOMETRY, 0, 0,
				     desc_buf, QUERY_DESC_MAX_SIZE);
	ufshcd_rpm_put_sync(hba);
	if (!ret)
		rw_size = desc_buf[GEOMETRY_DESC_PARAM_RPMB_RW_SIZE];
	kfree(desc_buf);

	dev_info(dev, "mt6853-rpmb: rw_size=%u\n", rw_size);
	mt6853_rpmb_dev_ops.reliable_wr_cnt = rw_size;

	if (scsi_device_get(mt6853_rpmb_sdev))
		return;

	rdev = rpmb_mtk_dev_register(dev, &mt6853_rpmb_dev_ops);
	if (IS_ERR(rdev)) {
		dev_info(dev, "mt6853-rpmb: register failed %ld\n",
			 PTR_ERR(rdev));
		scsi_device_put(mt6853_rpmb_sdev);
		return;
	}
	mt6853_rpmb_rawdev = rdev;
	scsi_device_put(mt6853_rpmb_sdev);
}

static void mt6853_rpmb_init(struct ufs_hba *hba)
{
	/* This serializes requests; no other path supplies an initial token. */
	sema_init(&mt6853_rpmb_sem, 1);
	async_schedule(mt6853_rpmb_add, hba);
}
#endif /* CONFIG_MTK_RPMB */

static int mt6853_init(struct ufs_hba *hba)
{
	struct mt6853_ufs_host *host;

	dev_info(hba->dev, "phase: variant init begin\n");
	host = devm_kzalloc(hba->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;
	host->infra = syscon_regmap_lookup_by_phandle(hba->dev->of_node,
						    "mediatek,infracfg");
	if (IS_ERR(host->infra))
		return dev_err_probe(hba->dev, PTR_ERR(host->infra),
				     "MT6853 infracfg unavailable\n");
	host->hs_gear = UFS_HS_G3;
	of_property_read_u32(hba->dev->of_node, "highspeed-gear", &host->hs_gear);
	/* Gear 4 adaptation and the old firmware's power-saving flow are pending. */
	host->hs_gear = clamp_t(u32, host->hs_gear, UFS_HS_G1, UFS_HS_G3);
	ufshcd_set_variant(hba, host);
	hba->caps &= ~(UFSHCD_CAP_CLK_GATING | UFSHCD_CAP_CLK_SCALING |
		       UFSHCD_CAP_RPM_AUTOSUSPEND | UFSHCD_CAP_CRYPTO |
		       UFSHCD_CAP_WB_EN);
	hba->quirks |= UFSHCD_QUIRK_BROKEN_AUTO_HIBERN8 |
		       UFSHCD_QUIRK_BROKEN_LSDBS_CAP;
	hba->rpm_lvl = UFS_PM_LVL_0;
	hba->spm_lvl = UFS_PM_LVL_0;
	dev_info(hba->dev, "MT6853 legacy firmware path, HS-G%u; PM/inline crypto disabled\n",
		 host->hs_gear);
#if IS_ENABLED(CONFIG_MTK_RPMB)
	mt6853_rpmb_init(hba);
#endif
	return 0;
}

static const struct ufs_hba_variant_ops mt6853_vops = {
	.name = "mt6853.ufshci",
	.init = mt6853_init,
	.setup_clocks = mt6853_setup_clocks,
	.hce_enable_notify = mt6853_hce_notify,
	.link_startup_notify = mt6853_link_notify,
	.pwr_change_notify = mt6853_pwr_notify,
	.device_reset = mt6853_device_reset,
};

static int mt6853_probe(struct platform_device *pdev)
{
	int ret;

	dev_info(&pdev->dev, "phase: platform init begin\n");
	ret = ufshcd_pltfrm_init(pdev, &mt6853_vops);
	dev_info(&pdev->dev, "phase: platform init returned %d\n", ret);

	if (!ret)
		pm_runtime_forbid(&pdev->dev);
	return ret;
}

/* No verified suspend/resume flow yet; keep DMA/firmware state from suspending. */
static int mt6853_suspend(struct device *dev)
{
	return -EBUSY;
}

static DEFINE_SIMPLE_DEV_PM_OPS(mt6853_pm_ops, mt6853_suspend, NULL);

static const struct of_device_id mt6853_match[] = {
	{ .compatible = "mediatek,mt6853-ufshci" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6853_match);

static struct platform_driver mt6853_driver = {
	.probe = mt6853_probe,
	.remove = ufshcd_pltfrm_remove,
	.driver = {
		.name = "ufs-mt6853",
		.of_match_table = mt6853_match,
		.pm = pm_sleep_ptr(&mt6853_pm_ops),
	},
};
module_platform_driver(mt6853_driver);

MODULE_DESCRIPTION("MT6853 UFS host with legacy 4.14 firmware interface");
MODULE_LICENSE("GPL");
