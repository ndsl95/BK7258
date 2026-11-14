# PCB1 自动背光 · 2026-11-14

P1 原理图新增「09 自动背光」电路块。U9 是 [VEML7700-TT（C1850416）](https://jlcpcb.com/partdetail/VishayIntertech-VEML7700TT/C1850416)，固定 7 位 I²C 地址 `0x10`。1 脚接 `TP_SCL`（P20），4 脚接 `TP_SDA`（P21），复用 R11/R12 的 4.7 kΩ 上拉；2 脚接 `VDD_3V3`，3 脚接 `GND`。C29 是 100 nF 0603（C14663），跨接供电与地。[Vishay 推荐电路](https://www.vishay.com/docs/84323/designingveml7700.pdf)

PCB 和固件均未改动。同步 PCB 后，将 U9 放到背面靠板边、天线禁铜区之外，确认侧向感光面朝壳体透光窗。C29 靠近 U9 的 2、3 脚。
