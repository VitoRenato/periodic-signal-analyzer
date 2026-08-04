#!/usr/bin/env python3
"""
AD9226 噪声/重复性/偏置 测试脚本

功能:
  1. 自动连接 /dev/ttyUSB0
  2. 连续读 N 帧 summary (不触发 dump, 只读 mean/min/max/pp)
  3. 统计 mean/min/max/pp 在 N 帧内的均值和标准差
  4. 估算 RMS 噪声、ENOB、FS-SNR

用法:
  1. 把 AD9226 输入端短路到 GND (测噪声)
     或接已知 DC 电压 (测偏置)
  2. python3 measure_noise.py [--port /dev/ttyUSB0] [--frames 100]
  3. 按 Ctrl+C 提前结束并打印已收集结果
"""

import argparse
import sys
import time
import threading

import serial
import numpy as np

# 标定参数 (从 AD9226_标定参数_H7.md 来)
ZERO_CODE = 2055
SCALE_UV_PER_CODE = 5236.0  # µV / code
FULL_SCALE_CODES = 4096


class SummaryReader:
    """持续读 F407 串口, 提取 'mean=... min=... max=... pp=... Fs=...' 一行"""

    def __init__(self, port, baud=115200):
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
        self.running = True
        self.summary = None
        self.lock = threading.Lock()
        self.frame_count = 0
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()

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
                    line = line.decode('utf-8', errors='ignore').strip()
                    if line.startswith('mean='):
                        d = {}
                        for p in line.split():
                            if '=' in p:
                                k, v = p.split('=', 1)
                                # 先尝试 int (支持 0x 前缀),再尝试 float,失败留作字符串
                                try:
                                    d[k] = int(v, 0) if v.startswith('0x') else int(v)
                                except ValueError:
                                    try:
                                        d[k] = float(v)
                                    except ValueError:
                                        d[k] = v
                        with self.lock:
                            self.summary = d
                            self.frame_count += 1
            except Exception as e:
                print(f"[serial error] {e}")
                break

    def get_summary(self, timeout=2.0):
        """阻塞直到拿到一帧新 summary"""
        n0 = self.frame_count
        t0 = time.time()
        while time.time() - t0 < timeout:
            with self.lock:
                if self.frame_count > n0:
                    return self.summary.copy()
            time.sleep(0.02)
        return None

    def close(self):
        self.running = False
        self.thread.join(timeout=1)
        self.ser.close()


def print_table(headers, rows, col_widths=None):
    if col_widths is None:
        col_widths = [max(len(str(h)), max((len(str(r[i])) for r in rows), default=0))
                      for i, h in enumerate(headers)]
    sep = '+' + '+'.join('-' * (w + 2) for w in col_widths) + '+'
    print(sep)
    print('| ' + ' | '.join(f"{h:<{col_widths[i]}}" for i, h in enumerate(headers)) + ' |')
    print(sep)
    for r in rows:
        print('| ' + ' | '.join(f"{str(r[i]):<{col_widths[i]}}" for i in range(len(headers))) + ' |')
    print(sep)


def main():
    parser = argparse.ArgumentParser(description='AD9226 噪声/重复性测试')
    parser.add_argument('--port', default='/dev/ttyUSB0', help='串口路径')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--frames', type=int, default=100, help='采样帧数')
    parser.add_argument('--interval', type=float, default=0.5, help='每帧间隔秒数')
    args = parser.parse_args()

    print(f"=== AD9226 噪声/重复性测试 ===")
    print(f"串口: {args.port} @ {args.baud}")
    print(f"帧数: {args.frames}, 间隔: {args.interval}s")
    print(f"标定: zero_code={ZERO_CODE}, scale={SCALE_UV_PER_CODE} µV/code")
    print(f"输入: 请把 AD9226 输入端短路到 GND (测噪声), 或接已知电压 (测偏置)")
    print()

    try:
        reader = SummaryReader(args.port, args.baud)
    except Exception as e:
        print(f"[error] 无法打开串口 {args.port}: {e}")
        print("  - 确认已加 dialout 组: groups $USER | grep dialout")
        print("  - 临时方案: sudo chmod 666 /dev/ttyUSB0")
        sys.exit(1)

    # === Drain 残留帧 (F407 printf 走 newlib-nano buffer,可能滞留老数据) ===
    # 修历史 bug: 切输入后,Python 启动时 F407 TX buffer 里可能还有上一轮的
    # summary 行,会让第一轮 noise 测试混进一个 pp=780 / min=1279 的脏数据
    time.sleep(0.3)                      # 等 F407 至少吐 1 帧
    reader.ser.reset_input_buffer()      # 清空 UART 接收 buffer
    reader.frame_count = 0               # 重置计数,后续 frame 从 0 开始计
    # === Drain 结束 ===

    print("等待 F407 输出第一帧...")
    s = reader.get_summary(timeout=3.0)
    if s is None:
        print("[error] 3 秒内没收到 summary, 检查 F407 是否在跑")
        reader.close()
        sys.exit(1)
    print(f"  首帧: {s}")
    print()

    print(f"开始收集 {args.frames} 帧...")
    means, mins, maxs, pps = [], [], [], []
    stds = []
    fs_list = []
    start = time.time()
    try:
        for i in range(args.frames):
            s = reader.get_summary(timeout=3.0)
            if s is None:
                print(f"  [warn] 第 {i+1} 帧超时")
                continue
            means.append(s.get('mean', 0))
            mins.append(s.get('min', 0))
            maxs.append(s.get('max', 0))
            pps.append(s.get('pp', 0))
            # std 可能是空字符串(老固件)、float、int,统一转成 float,失败用 NaN
            raw_std = s.get('std', None)
            if raw_std is None or raw_std == '':
                stds.append(float('nan'))
            else:
                try:
                    stds.append(float(raw_std))
                except (ValueError, TypeError):
                    stds.append(float('nan'))
            fs_list.append(s.get('Fs', 0))
            if (i + 1) % 10 == 0:
                print(f"  [{i+1}/{args.frames}] mean={s.get('mean')} pp={s.get('pp')} std={s.get('std')}")
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\n  用户中断")

    elapsed = time.time() - start
    reader.close()

    if not means:
        print("[error] 没有收集到任何帧")
        sys.exit(1)

    means = np.array(means, dtype=np.float64)
    mins = np.array(mins, dtype=np.float64)
    maxs = np.array(maxs, dtype=np.float64)
    pps = np.array(pps, dtype=np.float64)
    stds = np.array(stds, dtype=np.float64)
    fs_arr = np.array(fs_list, dtype=np.float64)

    has_std = not np.all(np.isnan(stds))

    n = len(means)
    print()
    print("=" * 60)
    print(f"  收集到 {n} 帧, 耗时 {elapsed:.1f}s ({elapsed/n*1000:.0f}ms/帧)")
    print("=" * 60)
    print()

    # === 偏置误差 (DC) ===
    print("【1】 偏置误差 (DC Offset)")
    mean_avg = np.mean(means)
    mean_std = np.std(means, ddof=1)
    offset_codes = mean_avg - ZERO_CODE
    offset_uV = offset_codes * SCALE_UV_PER_CODE
    print(f"  mean 帧均值: {mean_avg:.3f} code")
    print(f"  mean 帧标准差: {mean_std:.4f} code  (即每次采样的 mean 抖动)")
    print(f"  偏置 (相对 zero_code): {offset_codes:+.3f} code = {offset_uV:+.1f} µV = {offset_uV/1000:+.3f} mV")
    print()

    # === 重复性 (短期) ===
    print("【2】 短期重复性")
    pp_avg = np.mean(pps)
    pp_std = np.std(pps, ddof=1)
    min_avg = np.mean(mins)
    max_avg = np.mean(maxs)
    print(f"  pp 帧均值: {pp_avg:.2f} code, 标准差: {pp_std:.4f} code")
    print(f"  min 帧均值: {min_avg:.2f}, max 帧均值: {max_avg:.2f}")
    print(f"  pp 抖动 (σ): {pp_std*SCALE_UV_PER_CODE/1000:.3f} mV")
    print()

    # === RMS 噪声估算 ===
    print("【3】 RMS 噪声估算")
    fs_uV = FULL_SCALE_CODES * SCALE_UV_PER_CODE / 1000  # 满量程 mV

    if has_std:
        # F407 直接给的单点 std (最准)
        std_avg = np.nanmean(stds)
        std_std = np.nanstd(stds, ddof=1) if len(stds) > 1 else 0
        rms_codes = std_avg
        rms_uV = rms_codes * SCALE_UV_PER_CODE
        rms_mV = rms_uV / 1000
        enob = np.log2(FULL_SCALE_CODES / rms_codes) if rms_codes > 0 else 0
        snr_fs = 20 * np.log10(FULL_SCALE_CODES / rms_codes) if rms_codes > 0 else 0
        print(f"  [直接] F407 上报的单点 std: 均值 {std_avg:.3f} code, 帧间 σ {std_std:.3f} code")
        print(f"  → 单点 RMS 噪声: {rms_codes:.3f} code = {rms_mV:.3f} mV")
    else:
        # 没有 std 时, 用帧均值反推 (有偏差)
        rms_codes_from_mean = mean_std * np.sqrt(FULL_SCALE_CODES)
        rms_codes = rms_codes_from_mean
        rms_mV = rms_codes * SCALE_UV_PER_CODE / 1000
        enob = np.log2(FULL_SCALE_CODES / rms_codes_from_mean) if rms_codes_from_mean > 0 else 0
        snr_fs = 20 * np.log10(FULL_SCALE_CODES / rms_codes_from_mean) if rms_codes_from_mean > 0 else 0
        print(f"  [间接] 帧均值反推: 单点 RMS 噪声 {rms_codes_from_mean:.3f} code = {rms_mV:.3f} mV")
        print(f"  (如需更准值, 请升级 F407 固件支持 std 输出)")

    print(f"  满量程: {fs_uV:.1f} mV ({FULL_SCALE_CODES} code × {SCALE_UV_PER_CODE} µV)")
    # 用 FS sine RMS (1448 code) 而非 FS peak (4096 code) 算 SNR,这是 IEEE 标准
    fs_sine_rms = FULL_SCALE_CODES / 2.0 / np.sqrt(2)
    snr_sine_dB = 20 * np.log10(fs_sine_rms / rms_codes) if rms_codes > 0 else 0
    enob_std = (snr_sine_dB - 1.76) / 6.02 if snr_sine_dB > 1.76 else 0
    print(f"  SNR (相对 FS sine RMS): {snr_sine_dB:.1f} dB (理想 12-bit = 74 dB)")
    print(f"  ENOB (IEEE 标准 = (SNR-1.76)/6.02): {enob_std:.2f} bit")
    # 理论 12-bit 量化噪声 std = 1 LSB / sqrt(12) = 0.289 LSB
    quant_noise_codes = 1.0 / np.sqrt(12)
    quant_noise_mV = quant_noise_codes * SCALE_UV_PER_CODE / 1000
    print(f"  理论 12-bit 量化噪声 std: {quant_noise_codes:.3f} code = {quant_noise_mV:.3f} mV")
    print(f"  实测 / 理论比值: {rms_codes/quant_noise_codes:.2f}x (越接近 1 越好)")
    print()

    # === 频率稳定性 ===
    print("【4】 采样率稳定性")
    fs_avg = np.mean(fs_arr)
    fs_std = np.std(fs_arr, ddof=1)
    print(f"  Fs 帧均值: {fs_avg:.0f} Hz, 标准差: {fs_std:.2f} Hz ({fs_std/fs_avg*1e6:.2f} ppm)")
    print()

    # === 总结表 ===
    print("【5】 误差汇总")
    headers = ['指标', '数值 (code)', '数值 (mV)', '说明']
    rows = [
        ['DC 偏置', f"{offset_codes:+.3f}", f"{offset_uV/1000:+.3f}",
         "0V 输入时 mean - zero_code"],
        ['pp 重复性 σ', f"{pp_std:.4f}", f"{pp_std*SCALE_UV_PER_CODE/1000:.4f}",
         "相邻帧 pp 抖动 (短期)"],
        ['RMS 噪声', f"{rms_codes:.3f}", f"{rms_mV:.4f}",
         "F407 上报" if has_std else "帧均值反推"],
        ['ENOB', '-', '-', f"{enob_std:.2f} bit (理想 12-bit)"],
        ['SNR@FS sine', '-', '-', f"{snr_sine_dB:.1f} dB (理想 ~74 dB)"],
        ['Fs 抖动', f"{fs_std:.2f}", '-', f"{fs_std/fs_avg*1e6:.2f} ppm"],
    ]
    print_table(headers, rows)
    print()

    # === 保存原始数据 ===
    from datetime import datetime
    out_csv = f"noise_test_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    with open(out_csv, 'w') as f:
        f.write("frame,mean,min,max,pp,std,Fs\n")
        for i in range(n):
            std_val = f"{stds[i]:.3f}" if not np.isnan(stds[i]) else ""
            f.write(f"{i+1},{int(means[i])},{int(mins[i])},{int(maxs[i])},{int(pps[i])},{std_val},{int(fs_arr[i])}\n")
    print(f"[saved] 原始数据: {out_csv}")
    print()
    print("=== 测试完成 ===")
    print("如需分析波形 (单点噪声分布、FFT), 请用 viewer 触发 dump 后分析 CSV")


if __name__ == '__main__':
    main()
