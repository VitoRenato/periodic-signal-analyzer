# AD9226 USB Serial Viewer

在 PC 端通过串口实时观测 F407+AD9226 采样数据的 Python 工具。

---

## 功能

- 自动枚举 `/dev/ttyUSB*`、`/dev/ttyACM*`
- 实时解析 F407 的统计行(mean/min/max/pp/Fs)
- 发送 `d` 触发 F407 dump 一帧波形
- 绘制:
  - 时域波形
  - ADC 码值直方图
  - FFT 频谱(自动标出主峰频率)
- 计算并显示峰峰值、电压范围、主频
- 保存波形为 PNG 和 CSV
- 连续自动捕获模式

---

## 依赖与权限

系统已安装 Python 3,再装以下包:

```bash
pip3 install --user --break-system-packages pyserial numpy matplotlib
```

> 如果系统不允许 pip,也可用 apt:`sudo apt install python3-serial python3-numpy python3-matplotlib`

### 串口权限

`/dev/ttyUSB*` 默认需要 `dialout` 组权限。**不要 sudo 运行本脚本**,否则 root 找不到你用户目录下 pip 安装的 matplotlib。

**永久修复(推荐):**

```bash
sudo usermod -a -G dialout $USER
```

然后**重新登录**一次,使权限生效。

**临时修复(立即生效,重启后失效):**

```bash
sudo chmod 666 /dev/ttyUSB0
```

---

## 用法

### 1. 先烧录带 dump 功能的 F407 固件

```powershell
# 从仓库根目录执行；或先在 VS Code 中打开 AD9226_driver3/AD9226_driver
Set-Location .\AD9226_driver3\AD9226_driver
make flash
```

### 2. 给 AD9226 接信号源

例如函数发生器 1kHz 正弦,接到模块 SMA 口。

### 3. 运行 viewer

```powershell
# 保持在 AD9226_driver3/AD9226_driver 目录
python3 .\tools\ad9226_viewer.py
```

如果有多路串口,手动指定:

```bash
python3 ad9226_viewer.py /dev/ttyUSB0
```

### 4. 交互命令

```text
> c              # 触发 F407 dump 一帧,并画图
> s              # 显示最新统计
> h              # 绘制 mean/pp 历史曲线
> save           # 保存最新一帧到 CSV
> a              # 每 2 秒自动捕获,按 Ctrl+C 停止
> q              # 退出
```

---

## 输出示例

按 `c` 后会弹出一个图,同时终端显示:

```text
帧统计:
  采样数: 128
  code 范围: 1800 ~ 2300
  电压范围: -1.335 V ~ 1.281 V
  峰峰值 Upp: 2.616 V
  FFT 主频: 1.02 kHz
[saved] ad9226_frame_20250730_183012.png
```

---

## 文件说明

| 文件 | 说明 |
|------|------|
| `ad9226_viewer.py` | 主程序 |
| `ad9226_frame_*.png` | 每次 `c` 自动保存的波形图 |
| `ad9226_frame_*.csv` | `save` 命令保存的原始数据 |

CSV 格式:

```csv
index,code,uV
0,2055,0
1,2246,1000000
...
```

---

## 注意事项

- 该工具假设 F407 固件已包含 `dump_frame_csv()` 功能(向串口发 `d` 触发)。
- 标定参数写在脚本常量里:ZERO_CODE=2055, SCALE_UV_PER_CODE=5236。若后续重新标定,请同步修改这两个值。
- 图形界面需要显示器;纯 SSH 环境需配置 X11 转发,或改用 CSV 保存后离线分析。
