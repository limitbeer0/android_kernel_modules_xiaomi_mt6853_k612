#!/usr/bin/env python3
"""Apply the stock DTBO and validate first-stage cannon bindings after overlay."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


def get(dtb, kind, node, prop):
    return subprocess.check_output(["fdtget", "-t", kind, str(dtb), node, prop], text=True).strip()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("dtb", type=Path)
    p.add_argument("stock_dtbo", type=Path)
    p.add_argument("--index", type=int, default=0)
    args = p.parse_args()
    data = args.stock_dtbo.read_bytes()
    magic, total, header, size, count, offset, _, _ = struct.unpack_from(">8I", data)
    if magic != 0xd7b7ab1e or total > len(data) or header < 32 or size < 32:
        raise SystemExit("Invalid stock DTBO table")
    if not 0 <= args.index < count:
        raise SystemExit("DTBO index out of range")
    length, start = struct.unpack_from(">2I", data, offset + args.index * size)
    if start + length > total:
        raise SystemExit("Truncated DTBO entry")
    with tempfile.TemporaryDirectory(prefix="cannon-dt-") as temp:
        overlay, merged = Path(temp) / "overlay.dtbo", Path(temp) / "merged.dtb"
        overlay.write_bytes(data[start:start+length])
        subprocess.run(["fdtoverlay", "-i", str(args.dtb), "-o", str(merged), str(overlay)], check=True)
        # MT6360's stock overlay keeps legacy client names and flat profiles.
        subpmic = "/i2c5@11d00000/subpmic@34"
        properties = subprocess.check_output(
            ["fdtget", "-p", str(merged), subpmic], text=True).splitlines()
        if "mediatek,pmic-regulators" in properties:
            if get(merged, "s", "/i2c5@11d00000", "compatible") != "mediatek,mt6853-i2c":
                raise SystemExit("MT6360 needs the native MT6853 I2C adapter")
            if get(merged, "s", subpmic, "compatible") not in (
                    "mediatek,subpmic", "mediatek,mt6360"):
                raise SystemExit("Unexpected MT6360 compatible after stock overlay")
            if (get(merged, "u", subpmic, "interrupts") != "10 8" or
                    get(merged, "x", subpmic, "interrupt-parent") !=
                    get(merged, "x", "/pinctrl", "phandle")):
                raise SystemExit("MT6360 IRQB must be GPIO10 active-low")
            for prop, node in (("mediatek,pmic-regulators", "/mt6360_pmic_dts"),
                               ("mediatek,ldo-regulators", "/mt6360_ldo_dts")):
                if get(merged, "x", subpmic, prop) != get(merged, "x", node, "phandle"):
                    raise SystemExit("MT6360 stock regulator profile reference changed")
            for node in ("/mt6360_pmic_dts/buck1", "/mt6360_pmic_dts/ldo6",
                         "/mt6360_pmic_dts/ldo7", "/mt6360_ldo_dts/ldo2"):
                subprocess.run(["fdtget", str(merged), node, "regulator-always-on"],
                               check=True, stdout=subprocess.DEVNULL)
            if (get(merged, "x", "/battery", "charger") !=
                    get(merged, "x", subpmic + "/charger", "phandle")):
                raise SystemExit("Battery charger phandle changed after stock overlay")
            if get(merged, "s", "/mt6360_ldo_dts/ldo1", "regulator-name") != "VFP":
                raise SystemExit("Fingerprint VFP rail mapping changed")
        touch = "/spi4@11018000/touch_spi@0"
        if get(merged, "s", touch, "compatible") != "xiaomi,spi-for-tp":
            raise SystemExit("Stock touch transport compatible changed")
        if get(merged, "u", touch, "novatek,spi-frequency") != "5000000":
            raise SystemExit("Cannon Novatek chip must use 5 MHz")
        pio = get(merged, "x", "/pinctrl", "phandle")
        seninf = "/seninf_top@1a004000"
        camera_properties = subprocess.check_output(
            ["fdtget", "-p", str(merged), seninf], text=True).splitlines()
        if "mediatek,platform" in camera_properties:
            for bus in ("/i2c2@11d21000", "/i2c4@11d22000",
                        "/i2c8@11d02000", "/i2c9@11d03000"):
                if get(merged, "s", bus, "compatible") != "mediatek,mt6853-i2c":
                    raise SystemExit("Cannon camera I2C adapter compatible changed after overlay")
            if (get(merged, "s", seninf, "mediatek,platform") != "mt6853" or
                    get(merged, "s", seninf, "mediatek,seninf_max_num") != "6"):
                raise SystemExit("Cannon requires the MT6853 six-port SENINF layout")
            clock_names = get(merged, "s", seninf, "clock-names").split()
            clocks = get(merged, "x", seninf, "clocks").split()
            top = get(merged, "x", "/syson@10000000", "phandle")
            if (len(clock_names) != 19 or clock_names[-1] != "cannon-isp" or
                    len(clocks) != 38 or clocks[-2:] != [top, "a3"]):
                raise SystemExit("SENINF fixed ISP clock lost after signed overlay")
            spm_phandle = get(merged, "x", "/power-controller@10006000", "phandle")
            if get(merged, "x", seninf, "power-domains") != spm_phandle + " f":
                raise SystemExit("SENINF must use the native CAM power domain")
            if any(name.startswith("SCP_SYS_") for name in get(merged, "s", seninf, "clock-names").split()):
                raise SystemExit("SENINF still contains legacy SCPSYS clock IDs")
            for node, compatible, domain in (
                    ("/camsys@1a000000", "mediatek,mt6853-camsys_main", "f"),
                    ("/camsys_a@1a04f000", "mediatek,mt6853-camsys_rawa", "10"),
                    ("/camsys_b@1a06f000", "mediatek,mt6853-camsys_rawb", "11")):
                if (get(merged, "s", node, "compatible") != compatible or
                        get(merged, "x", node, "power-domains") != spm_phandle + " " + domain):
                    raise SystemExit("Camera clock provider/domain changed after signed overlay")
        if get(merged, "x", "/m4u@14016000", "mediatek,larbs") != "81 84 8d 90 91 92 93 94 95":
            raise SystemExit("Camera LARBs missing from native IOMMU after stock overlay")
        for node, domain in (("/smi_larb13@1a001000", "f"),
                             ("/smi_larb14@1a002000", "f"),
                             ("/smi_larb16@1a00f000", "10"),
                             ("/smi_larb17@1a010000", "11")):
            if (get(merged, "s", node, "clock-names") != "apb smi gals0 gals1" or
                    get(merged, "x", node, "power-domains") !=
                    get(merged, "x", "/power-controller@10006000", "phandle") + " " + domain):
                raise SystemExit("Camera SMI clock/domain binding lost after stock overlay")
        m4u = get(merged, "x", "/m4u@14016000", "phandle")
        for larb, ports in ((2, range(4)), (9, range(26)), (11, range(15, 26)), (13, range(9)), (16, range(17)), (17, range(17))):
            node = "/cannon-camera-dma-larb" + str(larb)
            if get(merged, "s", node, "compatible") != "xiaomi,cannon-camera-dma":
                raise SystemExit("Unexpected camera DMA mapper compatible")
            expected = []
            for port in ports:
                expected += [m4u, format(0x20000 | (larb << 5) | port, "x")]
            if get(merged, "x", node, "iommus").split() != expected:
                raise SystemExit("Camera DMA IOMMU ports changed after signed overlay")
        for provider in ("/syson@15020000", "/syscon@15820000"):
            props = subprocess.check_output(
                ["fdtget", "-p", str(merged), provider], text=True).splitlines()
            if "power-domains" in props:
                raise SystemExit("IMG clock provider must not depend on its SCPSYS consumer")
        for node, domain in (("/imgsys_config@15020000", "7"),
                             ("/imgsys2_config@15820000", "8")):
            if get(merged, "x", node, "power-domains") != spm_phandle + " " + domain:
                raise SystemExit("DIP consumer power domain missing after signed overlay")
        if get(merged, "x", "/imgsys_config@15020000", "iommus") != "83 20120":
            raise SystemExit("DIP must use real LARB9 CAM IOMMU domain")
        if get(merged, "x", "/dip_a0@15021000", "interrupts") != "0 15a 4":
            raise SystemExit("DIP_A must retain Cannon SPI346")
        mdp = "/cannon-mdp@1f000000"
        if (get(merged, "s", mdp, "compatible").split()[0] != "xiaomi,cannon-mdp" or
                get(merged, "u", mdp, "thread-count") != "24" or
                get(merged, "u", mdp, "mboxes") !=
                "82 19 0 1 82 20 0 1 82 21 0 1 82 22 0 1" or
                get(merged, "x", mdp, "mediatek,mailbox-gce") != "52"):
            raise SystemExit("Cannon MDP must use real normal GCE threads19..22")
        if (get(merged, "x", "/mdpsys_config@1f000000", "iommus") !=
                "83 20040 83 20041 83 20042 83 20043"):
            raise SystemExit("MDP data imports must use the real LARB2 IOMMU domain")
        if get(merged, "u", "/cannon-mdp-fixed-opp/opp-594000000", "opp-microvolt") != "725000":
            raise SystemExit("MDP measured594MHz requires the725mV fixed floor")
        dvfsrc = "/dvfsrc@10012000"
        if get(merged, "s", dvfsrc, "compatible") != "mediatek,mt6853-dvfsrc":
            raise SystemExit("DVFSRC must use the native MT6853 provider")
        subprocess.run(["fdtget", str(merged), dvfsrc, "xiaomi,cannon-legacy-firmware"],
                       check=True, stdout=subprocess.DEVNULL)
        vcore = dvfsrc + "/dvfsrc-vcore"
        for prop in ("regulator-min-microvolt", "regulator-max-microvolt"):
            if get(merged, "u", vcore, prop) != "725000":
                raise SystemExit("Fixed LK clocks require the 725mV Vcore floor")
        common = "/disp_smi_common@14002000"
        mmqos = "/cannon-mmqos"
        if (get(merged, "x", mmqos, "mediatek,commons-supply") !=
                get(merged, "x", common, "phandle") or
                get(merged, "x", common, "mmdvfs-dvfsrc-vcore-supply") !=
                get(merged, "x", vcore, "phandle")):
            raise SystemExit("MMQoS common/Vcore supplier reference changed after overlay")
        if (get(merged, "x", mmqos, "mediatek,larbs-supply") != "81 84 8d 90 91 92 93 94 95" or
                get(merged, "x", mmqos, "clocks") != "3e 9d"):
            raise SystemExit("Cannon MMQoS LARB/clock bindings changed after overlay")
        isp = "/camisp_legacy@1a000000"
        if (get(merged, "s", isp, "compatible") != "mediatek,camisp_legacy" or
                get(merged, "s", isp, "mediatek,platform") != "mt6853" or
                get(merged, "x", isp, "reg") != "0 1a000000 0 10000"):
            raise SystemExit("Native MT6853 ISP master binding changed by overlay")
        if len(get(merged, "s", isp, "clock-names").split()) != 18:
            raise SystemExit("MT6853 ISP must have 18 real clock consumers")
        for name, base, irq in (("cam1", 0x1a030000, 320), ("cam2", 0x1a050000, 321),
                               *[(f"camsv{i}", 0x1a090000 + i * 0x1000, irq)
                                 for i, irq in enumerate((327, 328, 329, 330, 334, 335), 2)]):
            node = f"/{name}_legacy@{base:x}"
            if get(merged, "u", node, "interrupts") != f"0 {irq} 4":
                raise SystemExit(f"ISP IRQ mapping changed: {node}")
        qos = "/cam_qos_legacy"
        if (get(merged, "x", qos, "dvfsrc-vcore-supply") != get(merged, "x", vcore, "phandle") or
                get(merged, "x", qos, "clocks") != "3e a3"):
            raise SystemExit("CAM QoS must use physical CAM clock and DVFSRC Vcore")
        if (get(merged, "u", "/cannon-cam-fixed-opp/opp-624000000", "opp-microvolt") != "725000" or
                get(merged, "x", "/cannon-cam-fixed-opp/opp-624000000", "opp-hz") != "0 25317c00"):
            raise SystemExit("CAM OPP must match the measured 624MHz/725mV floor")
        fingerprint_properties = subprocess.check_output(
            ["fdtget", "-p", str(merged), "/fingerprint"], text=True).splitlines()
        if "fpc,spi" in fingerprint_properties:
            spi = "/spi5@11019000/fingerprint@0"
            if get(merged, "x", "/fingerprint", "fpc,spi") != get(merged, "x", spi, "phandle"):
                raise SystemExit("FPC resources must reference the physical SPI5 device")
            if get(merged, "s", spi, "compatible") not in ("goodix,goodix-fp", "fpc,fpc1022"):
                raise SystemExit("Unexpected signed-overlay fingerprint transport")
            if get(merged, "x", "/fingerprint", "vfp-supply") != get(merged, "x", "/mt6360_ldo_dts/ldo1", "phandle"):
                raise SystemExit("FPC must use the original MT6360 VFP rail")
            for prop, gpio in (("reset-gpios", "2f"), ("irq-gpios", "30")):
                if get(merged, "x", "/fingerprint", prop) != pio + " " + gpio + " 0":
                    raise SystemExit("FPC reset/IRQ mapping changed after signed overlay")
        for prop, pin in (("novatek,reset-gpio", "f"), ("novatek,irq-gpio", "e")):
            if get(merged, "x", touch, prop) != pio + " " + pin + " 0":
                raise SystemExit("Cannon touch reset/IRQ GPIO mismatch")
        if get(merged, "s", touch, "novatek,boot-fw-name") != "novatek_ts_fw.bin":
            raise SystemExit("Cannon touch firmware selection changed")
        if get(merged, "u", touch, "super-resolution-factor") != "1":
            raise SystemExit("Cannon touch requires the normal NT36672A packet protocol")
        banks = [int(x, 16) for x in get(merged, "x", "/pinctrl", "reg").split()]
        addresses = [0x10005000, 0x11c30000, 0x11c20000, 0x11d10000,
                     0x11d30000, 0x11d40000, 0x11e20000, 0x11ea0000, 0x11f30000]
        expected = [cell for addr in addresses for cell in (0, addr, 0, 0x1000)]
        if banks != expected:
            raise SystemExit("GPIO MMIO bank order changed by overlay")
        if get(merged, "u", "/apirq@1000b000", "mediatek,total-pin-number") != "128":
            raise SystemExit("Incorrect EINT count")
        if get(merged, "u", "/apirq@1000b000", "interrupts") != "0 212 4":
            raise SystemExit("Incorrect EINT interrupt")
        if get(merged, "x", "/pinctrl", "mediatek,eint") != get(merged, "x", "/apirq@1000b000", "phandle"):
            raise SystemExit("PIO points at a different EINT controller")
        if "mediatek,mt6589-wdt" not in get(merged, "s", "/toprgu@10007000", "compatible").split():
            raise SystemExit("Missing common watchdog fallback")
        spm = "/power-controller@10006000"
        if (get(merged, "x", spm, "reg") != "0 10006000 0 1000" or
                get(merged, "u", spm, "#power-domain-cells") != "1"):
            raise SystemExit("SCPSYS must use the 6.12 SPM binding, not the legacy clock-provider register list")
        props = subprocess.check_output(["fdtget", "-p", str(merged), spm], text=True).splitlines()
        if "#clock-cells" in props or "mediatek,preserve-boot-power-state" not in props:
            raise SystemExit("SCPSYS boot power-state adoption is not configured")
        for node in ("/disp_smi_common@14002000", "/smi_larb0@14003000",
                     "/smi_larb1@14004000", "/m4u@14016000"):
            if get(merged, "x", node, "power-domains") != get(merged, "x", spm, "phandle") + " c":
                raise SystemExit(f"{node} must use the MT6853 display power domain")
        if get(merged, "s", "/m4u@14016000", "compatible") != "mediatek,mt6853-m4u":
            raise SystemExit("Missing MT6853 IOMMU provider binding")
        wrapper = "/gpufreq_wrapper"
        if get(merged, "s", wrapper, "compatible") != "mediatek,gpufreq_wrapper":
            raise SystemExit("Missing MT6853 gpufreq wrapper")
        for prop, expected in (("gpufreq-version", "2"), ("gpueb-support", "0"),
                               ("gpufreq-bringup", "0"), ("dual-buck", "0")):
            if get(merged, "u", wrapper, prop) != expected:
                raise SystemExit(f"Incorrect Cannon gpufreq setting: {prop}")
        if get(merged, "x", "/gpufreq", "clocks") != "3e ac 3e 2d 3e ab 7d 0":
            raise SystemExit("GPU clocks must exclude the old SCPSYS clock IDs")
        if get(merged, "s", "/gpufreq", "clock-names") != "clk_mux clk_main_parent clk_sub_parent subsys_bg3d":
            raise SystemExit("GPU clock names do not match the MT6853 driver")
        if "mediatek,mt6853-mfgcfg" not in get(merged, "s", "/syson@13fbf000", "compatible").split():
            raise SystemExit("Missing MT6853 MFG clock provider binding")
        for index, domain in ((2, 4), (3, 5), (5, 6)):
            node = f"/gpufreq_mfg{index}"
            if get(merged, "x", node, "power-domains") != get(merged, "x", spm, "phandle") + f" {domain:x}":
                raise SystemExit(f"{node} points at the wrong GPU power domain")
        for supply, phandle in (("_vgpu-supply", "7e"), ("_vsram_gpu-supply", "7f")):
            if get(merged, "x", "/gpufreq", supply) != phandle:
                raise SystemExit(f"Stock GPU rail changed: {supply}")
        keys = "/pwrap@10026000/mt6359-pmic/cannon-pmic-keys"
        if get(merged, "s", keys, "compatible") != "mediatek,mt6359p-keys":
            raise SystemExit("PMIC power key compatible changed by overlay")
        if get(merged, "u", keys + "/power", "linux,keycodes") != "116":
            raise SystemExit("Debug panic key is not KEY_POWER")
        host = "/ufshci@11270000"
        if "mediatek,mt6853-ufshci" not in get(merged, "s", host, "compatible").split():
            raise SystemExit("UFS compatible overwritten by overlay")
        if get(merged, "x", host, "mediatek,infracfg") != get(merged, "x", "/syson@10001000", "phandle"):
            raise SystemExit("UFS infracfg reset controller mismatch")
        # Clock providers translate legacy IDs, so DT/DTBO consumers keep 4.14 IDs.
        if get(merged, "x", host, "clocks") != "2b 4c 2b 3d 2b 3f 2b 4d 3e c7 3e 26 3e 2":
            raise SystemExit("UFS clocks no longer match the legacy ID lookup")
        if get(merged, "x", "/pwrap@10026000", "clocks") != "2b 2 2b 1 3e b9 3e 52":
            raise SystemExit("PWRAP clocks no longer match the legacy ID lookup")
        regulator = "/pwrap@10026000/mt6359-pmic/mt6359regulator/ldo_vemc"
        if get(merged, "s", regulator, "regulator-compatible") != "ldo-vemc":
            raise SystemExit("UFS VCC regulator does not match the 6.12 regulator driver")
        if get(merged, "x", host, "vcc-supply") != get(merged, "x", regulator, "phandle"):
            raise SystemExit("UFS VCC points at a different regulator")
        usb = "/mtu3_0@11200000"
        phy = "/usb0phy@11e40000/usb-phy@11e40000"
        if "xiaomi,cannon-mtu3" not in get(merged, "s", usb, "compatible").split():
            raise SystemExit("USB controller compatibility overwritten by overlay")
        if get(merged, "x", usb, "reg") != "0 11201000 0 2e00 0 11203e00 0 100" or get(merged, "s", usb, "reg-names") != "mac ippc":
            raise SystemExit("USB MAC/IPPC resources overlap or their register ranges changed")
        if get(merged, "s", usb, "dr_mode") != "peripheral" or get(merged, "s", usb, "maximum-speed") != "high-speed":
            raise SystemExit("USB diagnostic role/speed changed by overlay")
        if get(merged, "x", usb, "phys") != get(merged, "x", phy, "phandle") + " 3":
            raise SystemExit("USB controller does not reference the USB2 PHY")
        if get(merged, "x", phy, "reg") != "0 11e40000 0 700":
            raise SystemExit("USB2 PHY register bank changed by overlay")
        if get(merged, "s", "/usb0phy@11e40000", "compatible").split() != ["xiaomi,cannon-tphy", "mediatek,generic-tphy-v2"]:
            raise SystemExit("USB2 PHY bank version changed by overlay")
        if get(merged, "x", usb, "vusb33-supply") != get(merged, "x", "/pwrap@10026000/mt6359-pmic/mt6359regulator/ldo_vusb", "phandle"):
            raise SystemExit("USB VUSB supply mismatch")
        vibrator = "/odm/vibrator@0"
        vibr_supply = "/pwrap@10026000/mt6359-pmic/mt6359regulator/ldo_vibr"
        if not {"mediatek,vibrator", "regulator-vibrator"}.intersection(
                get(merged, "s", vibrator, "compatible").split()):
            raise SystemExit("Vibrator compatibility does not match its regulator driver")
        if get(merged, "s", vibrator, "label") != "vibrator":
            raise SystemExit("Official vibrator HAL LED name changed")
        if get(merged, "x", vibrator, "vib-supply") != get(merged, "x", vibr_supply, "phandle"):
            raise SystemExit("Vibrator does not reference the physical VIBR regulator")
        if any(int(get(merged, "x", vibrator, prop), 16) != 2800000
               for prop in ("min-volt", "max-volt")):
            raise SystemExit("Vibrator voltage differs from the verified 2.8 V setting")
        bootargs = get(merged, "s", "/chosen", "bootargs").split()
        for argument in ("cannon.block_fs_tools=1", "cannon.fscrypt_legacy_v1=1", "ramoops.mem_address=0x4d010000", "ramoops.mem_size=0xe0000",
                         "ramoops.console_size=0x40000", "ramoops.pmsg_size=0x10000",
                         "ramoops.record_size=0x1000", "ramoops.ftrace_size=0x1000"):
            if argument not in bootargs:
                raise SystemExit(f"Stock overlay removed {argument}")
        # These offsets are also present in the running 4.14 /proc/iomem.
        params = dict(x.split("=", 1) for x in bootargs if x.startswith("ramoops.") and "=" in x)
        addr = int(params["ramoops.mem_address"], 0)
        total = int(params["ramoops.mem_size"], 0)
        console = int(params["ramoops.console_size"], 0)
        ftrace = int(params["ramoops.ftrace_size"], 0)
        pmsg = int(params["ramoops.pmsg_size"], 0)
        dump = total - 2 * console - ftrace - pmsg
        if addr + dump != 0x4d05f000 or addr + dump + 2 * console + ftrace != 0x4d0e0000:
            raise SystemExit("pstore offsets differ from the working 4.14 reader")
    print(f"Stock DTBO index {args.index}: overlay and cannon first-stage binding checks passed")


if __name__ == "__main__":
    main()
