# SVR06 EE memory map (from a captured RAM image; values are layout facts only)

## Occupancy (256 KB blocks, % non-zero bytes)

    00000000: 48%  00040000:  1%  00080000:  3%  000c0000:  0%  00100000:100%  00140000:100%
    00180000:100%  001c0000:100%  00200000:100%  00240000:100%  00280000:100%  002c0000:100%
    00300000: 48%  00340000: 92%  00380000: 76%  003c0000: 12%  00400000: 14%  00440000:  1%
    00480000:  0%  004c0000: 70%  00500000:100%  00540000:100%  00580000: 53%  005c0000:  0%
    00600000: 34%  00640000:100%  00680000: 97%  006c0000: 99%  00700000:100%  00740000:100%
    00780000:100%  007c0000: 97%  00800000: 97%  00840000: 45%  00880000:  0%  008c0000: 25%
    00900000:100%  00940000: 91%  00980000: 37%  009c0000:100%  00a00000: 99%  00a40000: 95%
    00a80000:100%  00ac0000:100%  00b00000:100%  00b40000:100%  00b80000:100%  00bc0000:100%
    00c00000:100%  00c40000: 99%  00c80000: 99%  00cc0000: 92%  00d00000: 94%  00d40000: 93%
    00d80000: 98%  00dc0000: 72%  00e00000: 81%  00e40000: 74%  00e80000: 85%  00ec0000:100%
    00f00000: 99%  00f40000: 91%  00f80000: 85%  00fc0000: 85%  01000000: 83%  01040000: 85%
    01080000: 85%  010c0000: 83%  01100000: 85%  01140000: 85%  01180000: 83%  011c0000: 85%
    01200000: 70%  01240000:  0%  01280000:  0%  012c0000:  0%  01300000:  0%  01340000:  0%
    01380000:  0%  013c0000:  0%  01400000:  0%  01440000:  0%  01480000:  0%  014c0000:  0%
    01500000: 63%  01540000: 83%  01580000: 85%  015c0000: 85%  01600000: 83%  01640000: 85%
    01680000: 85%  016c0000: 83%  01700000: 85%  01740000: 85%  01780000: 85%  017c0000: 13%
    01800000:  0%  01840000:  0%  01880000: 37%  018c0000: 99%  01900000:100%  01940000:100%
    01980000:100%  019c0000:100%  01a00000: 97%  01a40000:100%  01a80000:100%  01ac0000: 99%
    01b00000: 98%  01b40000: 46%  01b80000:  0%  01bc0000:  0%  01c00000: 33%  01c40000: 85%
    01c80000:  0%  01cc0000: 43%  01d00000: 79%  01d40000:  0%  01d80000:  0%  01dc0000:  0%
    01e00000:  0%  01e40000: 20%  01e80000:  0%  01ec0000:  0%  01f00000:  0%  01f40000:  0%
    01f80000: 19%  01fc0000:  8%

## ELF segments placement

main image base 0x100000 size 0x299f00
4 KB pages identical to ELF in RAM: 621 / 666 (rest are written data)

## Overlay images resident (MWo3)

0x004d2c00  Menu.bin     text 0xbdbc0 data 0xf780 bss 0x2780
0x005a2700  Title.bin    text 0x25c0 data 0x200 bss 0x0

## Large free (zero) regions >= 1 MB

0x01234000 - 0x01510000  (2928 KB)
0x01d38000 - 0x01e69000  (1220 KB)
0x01e78000 - 0x01fa9000  (1220 KB)

## Addendum 2026-11-03
- 0x1AC780: 4x4 matrix-array multiply; loop top = function entry; caller 0x1AC960 (checkpoint-as-return case, CL-0108).
- 0x18C3B90: wrestler skeleton (115 bones); 0x196B4A0: wrestler skeleton (103 bones); referee skeleton 36 bones. skel+0xA0 local matrices, +0xA4 scratch/world-ish, +0xA8 bone defs (0x50 each, parent +0x30), +0xAC bone count, +0xB0 world.
- 0x166E80: CEventSystem::Play (hctp reference: drives entrance commentary).
- GIF IMAGE tag NLOOP = 15 bits (0x7FFF qwords max) - split in sceGsExecLoadImage.
- 0x10CC80: sqrtf(f12) -> f0 helper (SQRT.S at 0x10CC88); callers main 0x270E48, game ovl 0x4E47D8, MainGame 0x5D553C / 0x60D8C8 (CL-0110 fixed operand).
- 0x145788: function with SQRT.S at 0x1457A4 (f22) and 0x145860 (f12); callers 0x14F8A0, 0x15885C.
- 0x248770: function with SQRT.S at 0x248830 (f1); caller 0x248D0C (character pipeline range).
