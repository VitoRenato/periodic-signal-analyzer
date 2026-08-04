#!/usr/bin/env python3
"""
AD9226 USB Serial Viewer
========================
在 PC 端通过串口观测 F407+AD9226 的采样数据。

用法:
    python3 ad9226_viewer.py              # 自动找 /dev/ttyUSB*
    python3 ad9226_viewer.py /dev/ttyUSB0 # 指定端口

交互命令:
    c / capture    触发 F407 打印一帧,并画图
    s / summary    显示最新一帧的统计
    h / history    绘制最近 mean/pp 的历史曲线
    save           把最新一帧保存到 CSV
    a / auto       连续自动捕获(按 Ctrl+C 停止)
    q / quit       退出

依赖:
    pip3 install pyserial numpy matplotlib
"""

import glob
import sys
import threading
import time
from collections import deque
from datetime import datetime

import numpy as np

# matplotlib 可选导入:优先 TkAgg 弹窗,不行则退到 Agg 保存 PNG
HAS_MPL = False
MPL_BACKEND = None
plt = None

def _init_matplotlib():
    global HAS_MPL, MPL_BACKEND, plt
    try:
        import matplotlib
        matplotlib.use('TkAgg')
        import matplotlib.pyplot as _plt
        # 验证后端是否真的能工作(不弹窗)
        _fig = _plt.figure()
        _plt.close(_fig)
        plt = _plt
        MPL_BACKEND = 'TkAgg'
        HAS_MPL = True
        return
    except Exception as e:
        print(f"[warn] TkAgg 后端不可用: {e}")
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as _plt
        plt = _plt
        MPL_BACKEND = 'Agg'
        HAS_MPL = True
        print("[info] 已切换到 Agg 后端,图表将保存为 PNG 文件")
        return
    except Exception as e:
        print(f"[warn] matplotlib 完全不可用: {e}")
        print("       安装: pip3 install --user --break-system-packages matplotlib")

_init_matplotlib()


def find_port():
    """自动查找 /dev/ttyUSB* 或 /dev/ttyACM*"""
    ports = glob.glob('/dev/ttyUSB*') + glob.glob('/dev/ttyACM*')
    if not ports:
        raise RuntimeError("未找到 /dev/ttyUSB* 或 /dev/ttyACM* 串口")
    if len(ports) > 1:
        print(f"发现多个串口: {ports}, 默认用第一个")
    return ports[0]


class AD9226Viewer:
    ZERO_CODE = 2055
    SCALE_UV_PER_CODE = 5236
    DEFAULT_FS = 2048780

    def __init__(self, port, baud=115200):
        import serial
        import serial.serialutil
        try:
            # 先禁用 DTR/RTS 再 open,防止 pyserial 默认拉高 DTR/RTS 导致 STM32 复位
            self.ser = serial.Serial()
            self.ser.port = port
            self.ser.baudrate = baud
            self.ser.bytesize = serial.EIGHTBITS
            self.ser.parity = serial.PARITY_NONE
            self.ser.stopbits = serial.STOPBITS_ONE
            self.ser.timeout = 0.05
            self.ser.dtr = False
            self.ser.rts = False
            self.ser.open()
        except serial.serialutil.SerialException as e:
            print(f"\n[错误] 无法打开串口 {port}: {e}")
            print("\n常见原因和解决办法:")
            print("  1. 当前用户没有 /dev/ttyUSB* 读写权限")
            print("     永久修复: sudo usermod -a -G dialout $USER")
            print("     然后重新登录,再运行本脚本")
            print("  2. 临时修复(立即生效,重启失效):")
            print(f"     sudo chmod 666 {port}")
            print("  3. 串口被其他程序占用(如 tio、minicom),请先关闭")
            sys.exit(1)
        self.running = True
        self.lock = threading.Lock()

        self.summary = {}
        self.summary_history = {
            'mean': deque(maxlen=300),
            'pp': deque(maxlen=300),
            'min': deque(maxlen=300),
            'max': deque(maxlen=300),
        }
        self.summary_ts = deque(maxlen=300)

        self.dump_buffer = []
        self.dump_active = False
        self.last_frame = None
        self.last_frame_ts = None

        self.reader_thread = threading.Thread(target=self._reader, daemon=True)
        self.reader_thread.start()

    def _reader(self):
        buf = b''
        while self.running:
            try:
                chunk = self.ser.read(4096)
                if not chunk:
                    continue
                buf += chunk
                while b'\n' in buf:
                    line, buf = buf.split(b'\n', 1)
                    self._handle_line(line.decode('utf-8', errors='ignore').strip())
            except Exception as e:
                print(f"[serial error] {e}")

    def _handle_line(self, line):
        if '[DUMP]' in line or '[RAW]' in line:
            print(f"[debug] F407 ack: {line}")
            return
        if line.startswith('mean='):
            parts = line.split()
            d = {}
            for p in parts:
                if '=' in p:
                    k, v = p.split('=', 1)
                    try:
                        d[k] = int(v, 0) if v.startswith('0x') else int(v)
                    except ValueError:
                        d[k] = v
            with self.lock:
                self.summary = d
                self.summary_ts.append(time.time())
                for k in ['mean', 'pp', 'min', 'max']:
                    if k in d:
                        self.summary_history[k].append(d[k])
            return

        if '=== frame dump' in line:
            print(f"[debug] dump start: {line}")
            self.dump_active = True
            self.dump_buffer = []
            return
        if '=== end dump' in line:
            print(f"[debug] dump end, lines={len(self.dump_buffer)}")
            self.dump_active = False
            self._store_frame(self.dump_buffer)
            return
        # 新格式:=== RAW <seq> start === / === RAW <seq> end ===
        # 每行: "index,code,uV"
        if '=== RAW' in line and 'start' in line:
            print(f"[debug] raw dump start: {line}")
            self.dump_active = True
            self.dump_buffer = []
            return
        if '=== RAW' in line and 'end' in line:
            print(f"[debug] raw dump end, lines={len(self.dump_buffer)}")
            self.dump_active = False
            self._store_frame(self.dump_buffer)
            return
        if self.dump_active:
            self.dump_buffer.append(line)

    def _store_frame(self, lines):
        data = []
        for line in lines:
            parts = line.split(',')
            if len(parts) != 3:
                continue
            try:
                idx = int(parts[0])
                code = int(parts[1])
                uV = int(parts[2])
                data.append([idx, code, uV])
            except ValueError:
                pass
        if not data:
            return
        arr = np.array(data, dtype=np.int64)
        with self.lock:
            self.last_frame = arr
            self.last_frame_ts = datetime.now().strftime('%Y%m%d_%H%M%S')

    def trigger_dump(self, raw=True):
        """触发 F407 dump。
        raw=True (默认) → 发 'r',下一帧全 4096 点 raw (推荐,看真实波形)
        raw=False       → 发 'd',下一帧前 128 点 (兼容老固件)
        """
        cmd = b'r' if raw else b'd'
        print(f"[debug] sending '{cmd.decode()}' to {self.ser.port}")
        self.ser.write(cmd)
        self.ser.flush()  # 确保单个字节立即发出

    def wait_frame(self, timeout=10.0):
        """raw dump 4096 点 @ 115200 baud ≈ 9 秒,给足 timeout"""
        t0 = time.time()
        while time.time() - t0 < timeout:
            with self.lock:
                if self.last_frame is not None:
                    frame = self.last_frame
                    self.last_frame = None
                    return frame
            time.sleep(0.05)
        return None

    def get_summary(self):
        with self.lock:
            return self.summary.copy()

    def get_history(self):
        with self.lock:
            return {
                'ts': list(self.summary_ts),
                'mean': list(self.summary_history['mean']),
                'pp': list(self.summary_history['pp']),
                'min': list(self.summary_history['min']),
                'max': list(self.summary_history['max']),
            }

    def close(self):
        self.running = False
        self.reader_thread.join(timeout=1)
        self.ser.close()


def plot_frame(arr, fs=None, save_png=True):
    if not HAS_MPL:
        print("[warn] matplotlib 未安装,无法画图,请用 'save' 命令导出 CSV")
        return
    if arr is None or len(arr) == 0:
        print("没有可绘制的数据")
        return

    if fs is None:
        fs = AD9226Viewer.DEFAULT_FS

    idx = arr[:, 0]
    codes = arr[:, 1]
    uV = arr[:, 2]
    dt = 1.0 / fs
    t_ms = idx * dt * 1000.0
    N = len(codes)

    window = np.hanning(N)
    fft_vals = np.fft.rfft((codes - np.mean(codes)) * window)
    fft_mag = np.abs(fft_vals) * 2.0 / np.sum(window)
    freqs = np.fft.rfftfreq(N, dt)

    if len(fft_mag) > 1:
        peak_idx = int(np.argmax(fft_mag[1:]) + 1)
        peak_freq = freqs[peak_idx]
        peak_mag = fft_mag[peak_idx]
    else:
        peak_freq = 0
        peak_mag = 0

    fig, axes = plt.subplots(3, 1, figsize=(12, 10))

    ax = axes[0]
    ax.plot(t_ms, uV / 1000.0, 'b-', linewidth=0.8)
    ax.set_xlabel('时间 (ms)')
    ax.set_ylabel('电压 (mV)')
    ax.set_title(f'时域波形 (N={N}, Fs={fs/1e6:.3f} MHz)')
    ax.grid(True)

    ax = axes[1]
    ax.hist(codes, bins=64, color='green', edgecolor='black', alpha=0.7)
    ax.set_xlabel('ADC code')
    ax.set_ylabel('频数')
    ax.set_title('码值直方图')
    ax.grid(True)

    ax = axes[2]
    ax.semilogy(freqs[1:] / 1000.0, fft_mag[1:], 'r-', linewidth=0.8)
    ax.set_xlabel('频率 (kHz)')
    ax.set_ylabel('幅度')
    ax.set_title(f'FFT (主峰 {peak_freq/1000:.2f} kHz, 幅度 {peak_mag:.1f})')
    ax.grid(True)

    fig.tight_layout()
    png_name = f'ad9226_frame_{datetime.now().strftime("%Y%m%d_%H%M%S")}.png'
    fig.savefig(png_name, dpi=150)
    print(f"[saved] {png_name}")
    if MPL_BACKEND == 'TkAgg':
        plt.show()

    print(f"\n帧统计:")
    print(f"  采样数: {N}")
    print(f"  code 范围: {codes.min()} ~ {codes.max()}")
    print(f"  电压范围: {uV.min()/1e6:.3f} V ~ {uV.max()/1e6:.3f} V")
    print(f"  峰峰值 Upp: {(uV.max()-uV.min())/1e6:.3f} V")
    print(f"  FFT 主频: {peak_freq/1000:.2f} kHz")


def plot_history(hist):
    if not HAS_MPL:
        print("[warn] matplotlib 未安装,无法绘制历史曲线")
        return
    if not hist['mean']:
        print("还没有历史数据")
        return
    fig, axes = plt.subplots(2, 1, figsize=(10, 6), sharex=True)

    ax = axes[0]
    ax.plot(hist['mean'], 'b-', linewidth=0.8, label='mean')
    ax.axhline(AD9226Viewer.ZERO_CODE, color='gray', linestyle='--', label='zero')
    ax.set_ylabel('mean code')
    ax.set_title('mean / pp 历史')
    ax.legend()
    ax.grid(True)

    ax = axes[1]
    ax.plot(hist['pp'], 'r-', linewidth=0.8)
    ax.set_ylabel('pp code')
    ax.set_xlabel('frame index')
    ax.grid(True)

    fig.tight_layout()
    png_name = f'ad9226_history_{datetime.now().strftime("%Y%m%d_%H%M%S")}.png'
    fig.savefig(png_name, dpi=150)
    print(f"[saved] {png_name}")
    if MPL_BACKEND == 'TkAgg':
        plt.show()


def save_frame_csv(arr, filename=None):
    if arr is None or len(arr) == 0:
        print("没有可保存的数据")
        return
    if filename is None:
        filename = f'ad9226_frame_{datetime.now().strftime("%Y%m%d_%H%M%S")}.csv'
    np.savetxt(filename, arr, delimiter=',', fmt='%d',
               header='index,code,uV', comments='')
    print(f"[saved] {filename}")


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    print(f"AD9226 Viewer 连接 {port}")
    print("命令: c/capture 捕获画图 | s/summary 统计 | h/history 历史 | save CSV | a/auto 自动 | q/quit 退出")

    viewer = AD9226Viewer(port)

    try:
        while True:
            cmd = input("> ").strip().lower()
            if cmd in ('q', 'quit', 'exit'):
                break
            elif cmd in ('c', 'capture'):
                # c 默认走 'r' (4096 点), c1 走 'd' (128 点, 老固件兼容)
                use_raw = not cmd.endswith('1')
                viewer.trigger_dump(raw=use_raw)
                mode = 'r (4096 点)' if use_raw else 'd (128 点)'
                print(f"等待 F407 dump ({mode})... 首次 raw dump 约 9s")
                frame = viewer.wait_frame(timeout=15.0 if use_raw else 5.0)
                if frame is not None:
                    plot_frame(frame)
                else:
                    print("超时,未收到 dump(确认固件已烧录 raw dump 功能)")
            elif cmd in ('s', 'summary'):
                print(viewer.get_summary())
            elif cmd in ('h', 'history'):
                plot_history(viewer.get_history())
            elif cmd == 'save':
                use_raw = True
                viewer.trigger_dump(raw=use_raw)
                frame = viewer.wait_frame(timeout=15.0 if use_raw else 5.0)
                if frame is not None:
                    save_frame_csv(frame)
                else:
                    print("无数据")
            elif cmd in ('a', 'auto'):
                print("进入自动模式,每 2 秒捕获一帧(按 Ctrl+C 停止,raw dump 需 ~9s)...")
                try:
                    while True:
                        viewer.trigger_dump(raw=True)
                        frame = viewer.wait_frame(timeout=15.0)
                        if frame is not None:
                            plot_frame(frame, save_png=False)
                        time.sleep(2.0)
                except KeyboardInterrupt:
                    pass
                print("退出自动模式")
            else:
                print("未知命令,可用: c/s/h/save/a/q")
    except KeyboardInterrupt:
        pass
    finally:
        viewer.close()
        print("已关闭串口")


if __name__ == '__main__':
    main()

