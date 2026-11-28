#!/usr/bin/env python3
"""Generate disabled native APU2.5 consumers using Cannon legacy clock IDs."""
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
vendor=root/'kernel_device_modules-6.12'
mapping=(vendor/'drivers/clk/mediatek/clk-mt6853-cannon-ids.h').read_text()
top=mapping.split('static const u16 cannon_top_ids',1)[1].split('};',1)[0]
ids={name:int(index) for index,name in re.findall(r'\[(\d+)\] = (CLK_TOP_\w+) \+ 1',top)}
def clock(name):
    if name not in ids:
        raise SystemExit('No real Cannon clock: '+name)
    return f'<0x3e 0x{ids[name]:x}>'
def parents(names):
    return ', '.join(clock(name) for name in names.split())
text='''// SPDX-License-Identifier: GPL-2.0-only
// Prepared native MT6853 APU2.5 graph, NOT included by cannon.dts yet.
// All consumers stay disabled pending RPC/IOMMU/firmware integration.
// Clock references are translated by cannon/prepare_apu_dt.py.
// Fixed initial OPPs preserve the tested display/ISP Vcore floor.
/ {
 cannon_apu_core_opp: cannon-apu-core-opp {
  compatible = "operating-points-v2";
  opp-546000000 { opp-hz = /bits/ 64 <546000000>; opp-microvolt = <725000>; };
 };
 cannon_apu_conn_opp: cannon-apu-conn-opp {
  compatible = "operating-points-v2";
  opp-624000000 { opp-hz = /bits/ 64 <624000000>; opp-microvolt = <750000>; };
 };
 cannon_apu_vpu_opp: cannon-apu-vpu-opp {
  compatible = "operating-points-v2"; opp-shared;
  opp-624000000 { opp-hz = /bits/ 64 <624000000>; opp-microvolt = <750000>; };
 };
 cannon-apu-rpc@190f0000 {
  compatible = "mtk68xx,apurpc";
  status = "disabled";
  reg = <0 0x190f0000 0 0x1000>, <0 0x10000000 0 0x1000>, <0 0x10006000 0 0x1000>;
  cg-names = "vcore_cg_con", "conn_cg_con", "vpu0_cg_con", "vpu1_cg_con";
  cgs = <0 0x19029000 0 0>, <0 0x19020000 0 0>,
        <0 0x19030000 0 0x100>, <0 0x19031000 0 0x100>;
  clock-names = "conn", "vpu0", "vpu1", "vcore";
  clocks = @RPC_CLOCKS@;
  voltage-names = "vvpu", "vcore", "vsram";
  vcore-supply = <&cannon_dvfsrc_vcore>;
  vvpu-supply = <0x4c>;
  vsram-supply = <0x9f>;
 };
 cannon_apucore: APUCORE {
  compatible = "mtk6853,apucore";
  status = "disabled";
  operating-points-v2 = <&cannon_apu_core_opp>;
  vcore-supply = <&cannon_dvfsrc_vcore>;
  sys-mux = @CORE_CLOCK@;
  sys-mux-parent = @CORE_PARENTS@;
  gov = "apuuser";
  depth = <3>;
  cannon_apuconn: APUCONN {
   compatible = "mtk6853,apucon";
   status = "disabled";
   operating-points-v2 = <&cannon_apu_conn_opp>;
   vvpu-supply = <0x4c>;
   vsram-supply = <0x9f>;
   sys-mux = @CONN_CLOCK@;
   sys-mux-parent = @CONN_PARENTS@;
   devfreq = <&cannon_apucore>;
   gov = "apuconstrain";
   depth = <2>;
   consumer = <0x8c>;
  };
 };
};
&{/apusys_power} {
 APUCB {
  compatible = "mediatek,apusys_cb";
  status = "disabled";
 };
 cannon_apuvpu: APUVPU {
  compatible = "mtk68x3,apuvpu";
  status = "disabled";
  operating-points-v2 = <&cannon_apu_vpu_opp>;
  apmix-pll = <0x54 13>;
  mediatek,apmixed = <0x54>;
  sys-mux = @VPU_SYS@;
  top-mux = @VPU_TOP@;
  top-pll = @VPU_PLL@;
  devfreq = <&cannon_apuconn>;
  gov = "apupassive";
  depth = <1>;
  APUVPU@0 {
   compatible = "mtk68x3,apuvpu0";
   status = "disabled";
   operating-points-v2 = <&cannon_apu_vpu_opp>;
   top-mux = @VPU0_TOP@;
   devfreq = <&cannon_apuvpu>;
   gov = "apupassive"; depth = <0>;
  };
  APUVPU@1 {
   compatible = "mtk68x3,apuvpu1";
   status = "disabled";
   operating-points-v2 = <&cannon_apu_vpu_opp>;
   top-mux = @VPU1_TOP@;
   devfreq = <&cannon_apuvpu>;
   gov = "apupassive"; depth = <0>;
  };
 };
};
'''
values={
'RPC_CLOCKS':parents('CLK_TOP_DSP_SEL CLK_TOP_DSP1_NPUPLL_SEL CLK_TOP_DSP2_NPUPLL_SEL CLK_TOP_IPU_IF_SEL'),
'CORE_CLOCK':clock('CLK_TOP_IPU_IF_SEL'),
'CORE_PARENTS':parents('CLK_TOP_TCK_26M_MX9 CLK_TOP_UNIVPLL_D6_D2 CLK_TOP_MAINPLL_D4_D2 CLK_TOP_UNIVPLL_D4_D2 CLK_TOP_UNIVPLL_D6 CLK_TOP_MAINPLL_D4 CLK_TOP_TVDPLL CLK_TOP_UNIVPLL_D4'),
'CONN_CLOCK':clock('CLK_TOP_DSP_SEL'),
'CONN_PARENTS':parents('CLK_TOP_TCK_26M_MX9 CLK_TOP_MAINPLL_D9 CLK_TOP_UNIVPLL_D4_D2 CLK_TOP_UNIVPLL_D5 CLK_TOP_UNIVPLL_D4 CLK_TOP_UNIVPLL_D6 CLK_TOP_MAINPLL_D3 CLK_TOP_UNIVPLL_D3'),
'VPU_SYS':parents('CLK_TOP_DSP1_SEL_CK CLK_TOP_DSP2_SEL_CK'),
'VPU_TOP':parents('CLK_TOP_DSP1_NPUPLL_SEL CLK_TOP_DSP2_NPUPLL_SEL'),
'VPU_PLL':clock('CLK_TOP_NPUPLL'),
'VPU0_TOP':clock('CLK_TOP_DSP1_NPUPLL_SEL'),
'VPU1_TOP':clock('CLK_TOP_DSP2_NPUPLL_SEL'),
}
for key,value in values.items():
    text=text.replace('@'+key+'@',value)
assert '@RPC_' not in text
out=vendor/'arch/arm64/boot/dts/mediatek/cannon-apu-prep.dtsi'
out.write_text(text)
print('Prepared disabled APU graph:',out)
