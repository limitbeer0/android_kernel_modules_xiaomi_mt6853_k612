/* SPDX-License-Identifier: GPL-2.0-only */
/* MT6853: stable block indices, checked suppliers, publish only when ready. */
static int cannon_dip_shared_init(struct platform_device *pdev)
{
	struct clk **clks[] = { &dip_clk.DIP_IMG_LARB9, &dip_clk.DIP_IMG_LARB11,
		&dip_clk.DIP_IMG_DIP, &dip_clk.DIP_IMG_DIP_MSS, &dip_clk.DIP_IMG_MFB_DIP };
	const char *names[] = { "DIP_CG_IMG_LARB9", "DIP_CG_IMG_LARB11",
		"DIP_CG_IMG_DIP", "DIP_CG_IMG_DIP_MSS", "DIP_CG_IMG_MFB_DIP" };
	struct platform_device *larbs[2] = {};
	struct device_node *node;
	struct device_link *link;
	int i, j, ret = 0;

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		*clks[i] = devm_clk_get(&pdev->dev, names[i]);
		if (IS_ERR(*clks[i]))
			return PTR_ERR(*clks[i]);
	}
	for (i = 0; i < ARRAY_SIZE(larbs); i++) {
		node = of_parse_phandle(pdev->dev.of_node, "mediatek,larb", i);
		if (!node) {
			ret = -EINVAL;
			goto put_larbs;
		}
		larbs[i] = of_find_device_by_node(node);
		of_node_put(node);
		if (!larbs[i] || !device_is_bound(&larbs[i]->dev)) {
			ret = -EPROBE_DEFER;
			goto put_larbs;
		}
	}
	for (i = 0; i < ARRAY_SIZE(larbs); i++) {
		link = device_link_add(&pdev->dev, &larbs[i]->dev,
			DL_FLAG_PM_RUNTIME | DL_FLAG_AUTOREMOVE_CONSUMER);
		if (!link) {
			ret = -ENOMEM;
			goto put_larbs;
		}
	}
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(34));
	if (ret)
		goto put_larbs;
	dip_devs->larb9 = &larbs[0]->dev;
	dip_devs->larb11 = &larbs[1]->dev;

	spin_lock_init(&IspInfo.SpinLockIspRef);
	spin_lock_init(&IspInfo.SpinLockIsp);
	spin_lock_init(&IspInfo.SpinLockRTBC);
	spin_lock_init(&IspInfo.SpinLockClock);
	spin_lock_init(&SpinLock_P2FrameList);
	spin_lock_init(&SpinLockRegScen);
	spin_lock_init(&SpinLock_UserKey);
	for (i = 0; i < DIP_IRQ_TYPE_AMOUNT; i++) {
		spin_lock_init(&IspInfo.SpinLockIrq[i]);
		spin_lock_init(&IspInfo.SpinLockIrqCnt[i]);
		init_waitqueue_head(&IspInfo.WaitQueueHead[i]);
		for (j = 0; j < DIP_ISR_MAX_NUM; j++) {
			IspInfo.IrqCntInfo.m_err_int_cnt[i][j] = 0;
			IspInfo.IrqCntInfo.m_warn_int_cnt[i][j] = 0;
		}
	}
	init_waitqueue_head(&P2WaitQueueHead_WaitDeque);
	init_waitqueue_head(&P2WaitQueueHead_WaitFrame);
	init_waitqueue_head(&P2WaitQueueHead_WaitFrameEQDforDQ);
	dip_wake_lock = wakeup_source_register(&pdev->dev, "dip_lock_wakelock");
	if (!dip_wake_lock) {
		ret = -ENOMEM;
		goto put_larbs;
	}
	isp_mdp_wake_lock = wakeup_source_register(&pdev->dev, "isp_mdp_wakelock");
	if (!isp_mdp_wake_lock) {
		wakeup_source_unregister(dip_wake_lock);
		dip_wake_lock = NULL;
		ret = -ENOMEM;
		goto put_larbs;
	}
	for (i = 0; i < DIP_IRQ_TYPE_AMOUNT; i++)
		tasklet_init(dip_tasklet[i].pIsp_tkt, dip_tasklet[i].tkt_cb, 0);
#if (DIP_BOTTOMHALF_WORKQ == 1)
	for (i = 0; i < DIP_IRQ_TYPE_AMOUNT; i++) {
		dip_workque[i].module = i;
		INIT_WORK(&dip_workque[i].dip_bh_work, DIP_BH_Workqueue);
	}
#endif
	IspInfo.UserCount = 0;
	pm_runtime_enable(&pdev->dev);
	cannon_dip_shared = true;
put_larbs:
	for (i = 0; i < ARRAY_SIZE(larbs); i++)
		if (larbs[i])
			put_device(&larbs[i]->dev);
	return ret;
}

static int cannon_dip_publish(void)
{
	struct device *dev;
	int ret;

	if (!cannon_mdp_frontend_ready())
		return -EPROBE_DEFER;
	ret = DIP_RegCharDev();
	if (ret)
		return ret;
	pIspClass = class_create("dipdrv");
	if (IS_ERR(pIspClass)) {
		ret = PTR_ERR(pIspClass);
		pIspClass = NULL;
		goto unreg;
	}
	cmdqCoreRegisterCB(mdp_get_group_isp(), DIP_MDPClockOnCallback,
		DIP_MDPDumpCallback, DIP_MDPResetCallback, DIP_MDPClockOffCallback);
	cmdqCoreRegisterDebugRegDumpCB(DIP_BeginGCECallback, DIP_EndGCECallback);
	cannon_dip_callbacks = true;
	WRITE_ONCE(cannon_dip_ready, true);
	dev = device_create(pIspClass, NULL, IspDevNo, NULL, DIP_DEV_NAME);
	if (!IS_ERR(dev))
		return 0;
	ret = PTR_ERR(dev);
	WRITE_ONCE(cannon_dip_ready, false);
	cmdqCoreRegisterCB(mdp_get_group_isp(), NULL, NULL, NULL, NULL);
	cmdqCoreRegisterDebugRegDumpCB(NULL, NULL);
	cannon_dip_callbacks = false;
	class_destroy(pIspClass);
	pIspClass = NULL;
unreg:
	DIP_UnregCharDev();
	return ret;
}

static int cannon_dip_probe(struct platform_device *pdev)
{
	const struct of_device_id *match = of_match_device(dip_of_ids, &pdev->dev);
	struct resource *res;
	struct dip_device *block;
	int idx, ret = 0;

	if (!match)
		return -ENODEV;
	idx = match - dip_of_ids;
	if (idx >= 5)
		return -ENODEV;
	mutex_lock(&cannon_dip_probe_lock);
	if (idx && !cannon_dip_shared) {
		ret = -EPROBE_DEFER;
		goto out;
	}
	block = &dip_devs[idx];
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -EINVAL;
		goto out;
	}
	/* IMGSYS config aliases share their register bank with native CCF. */
	block->regs = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!block->regs) {
		ret = -ENOMEM;
		goto out;
	}
	block->dev = &pdev->dev;
	if (!idx) {
		ret = cannon_dip_shared_init(pdev);
		if (ret)
			goto clear;
	}
	if (idx == DIP_DIP_A_IDX) {
		block->irq = platform_get_irq(pdev, 0);
		if (block->irq < 0) {
			ret = block->irq;
			goto clear;
		}
		ret = devm_request_irq(&pdev->dev, block->irq, DIP_Irq_DIP_A,
			IRQF_NO_AUTOEN, "cannon-dip", block);
		if (ret)
			goto clear;
		IspInfo.IrqNum = block->irq;
	}
	cannon_dip_blocks |= BIT(idx);
	nr_dip_devs = hweight_long(cannon_dip_blocks);
	if (cannon_dip_blocks == GENMASK(4, 0)) {
		ret = cannon_dip_publish();
		if (ret) {
			cannon_dip_blocks &= ~BIT(idx);
			nr_dip_devs = hweight_long(cannon_dip_blocks);
			goto clear;
		}
		dev_info(&pdev->dev, "Cannon DIP ready: 5 indexed blocks, real LARB9/11, gated IRQ\n");
	}
	goto out;
clear:
	block->regs = NULL;
	block->dev = NULL;
out:
	mutex_unlock(&cannon_dip_probe_lock);
	return ret;
}

static void cannon_dip_remove(struct platform_device *pdev)
{
	const struct of_device_id *match = of_match_device(dip_of_ids, &pdev->dev);
	int idx = match - dip_of_ids, i;

	mutex_lock(&cannon_dip_probe_lock);
	if (cannon_dip_ready) {
		/* The endpoint owner pins this module while file operations run. */
		if (G_u4DipEnClkCnt)
			cannon_dip_clock(false);
		WRITE_ONCE(cannon_dip_ready, false);
		device_destroy(pIspClass, IspDevNo);
		class_destroy(pIspClass);
		pIspClass = NULL;
		DIP_UnregCharDev();
	}
	if (cannon_dip_callbacks) {
		cmdqCoreRegisterCB(mdp_get_group_isp(), NULL, NULL, NULL, NULL);
		cmdqCoreRegisterDebugRegDumpCB(NULL, NULL);
		cannon_dip_callbacks = false;
	}
	if (!idx && cannon_dip_shared) {
		for (i = 0; i < DIP_IRQ_TYPE_AMOUNT; i++)
			tasklet_kill(dip_tasklet[i].pIsp_tkt);
#if (DIP_BOTTOMHALF_WORKQ == 1)
		for (i = 0; i < DIP_IRQ_TYPE_AMOUNT; i++)
			cancel_work_sync(&dip_workque[i].dip_bh_work);
#endif
		wakeup_source_unregister(isp_mdp_wake_lock);
		wakeup_source_unregister(dip_wake_lock);
		pm_runtime_disable(&pdev->dev);
		cannon_dip_shared = false;
	}
	cannon_dip_blocks &= ~BIT(idx);
	nr_dip_devs = hweight_long(cannon_dip_blocks);
	mutex_unlock(&cannon_dip_probe_lock);
}
