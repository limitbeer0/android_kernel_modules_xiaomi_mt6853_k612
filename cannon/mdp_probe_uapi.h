// SPDX-License-Identifier: GPL-2.0-only
/* Probe-only ABI structs copied from native mdp_def.h. */
#include <stdbool.h>
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint64_t cmdqU32Ptr_t;
#define CMDQ_ISP_META_CNT 8
struct cmdqMetaBuf {
	uint64_t va;
	uint64_t size;
};
struct cmdqSecIspMeta {
	struct cmdqMetaBuf ispBufs[CMDQ_ISP_META_CNT];
	uint64_t CqSecHandle;
	uint32_t CqSecSize;
	uint32_t CqDesOft;
	uint32_t CqVirtOft;
	uint64_t TpipeSecHandle;
	uint32_t TpipeSecSize;
	uint32_t TpipeOft;
	uint64_t BpciHandle;
	uint64_t LsciHandle;
	uint64_t LceiHandle;
	uint64_t DepiHandle;
	uint64_t DmgiHandle;
};
struct cmdqSecDataStruct {
	bool is_secure;		/* [IN]true for secure command */

	/* address metadata, used to translate secure buffer PA
	 * related instruction in secure world
	 */
	uint32_t addrMetadataCount;	/* [IN] count of element in addrList */
	/* [IN] array of cmdqSecAddrMetadataStruct */
	cmdqU32Ptr_t addrMetadatas;
	uint32_t addrMetadataMaxCount;	/*[Reserved] */

	uint64_t enginesNeedDAPC;
	uint64_t enginesNeedPortSecurity;

	/* [Reserved] This is for CMDQ driver usage itself. Not for client.
	 * task index in thread's tasklist. -1 for not in tasklist.
	 */
	int32_t waitCookie;
	/* reset HW thread in SWd */
	bool resetExecCnt;

	/* ISP metadata for secure camera */
	struct cmdqSecIspMeta ispMeta;

	/* client extension feature */
	uint64_t extension;
};
#include "../kernel_device_modules-6.12/drivers/misc/mediatek/mdp/mdp_def_ex.h"
