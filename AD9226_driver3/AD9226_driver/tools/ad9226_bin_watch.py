#!/usr/bin/env python3
"""
AD9226 Binary Mirror Viewer
============================
读 F407 通过 UART 镜像出来的二进制帧(和 F407→H7 SPI 收到同一份数据)。

协议 (C 端 dump_frame_binary 定义):
    STX    = 0xAA 0x55           (2B, 大端字节序的"视觉对称")
    seq    = uint32_t LE         (4B)  帧序号
    fs_hz  = uint32_t LE         (4B)  采样率
    n      = uint16_t LE         (2B)  样本数 (4096)
    codes  = uint16_t LE × n     (n*2B) raw 12-bit code
    ETX    = 0x55 0xAA           (2B)
    合计: 12 + 2*n + 2 = 8206 B/帧 @ 921600 baud = 71ms/帧

F407 端:
    - 默认开 continuous binary 镜像,每帧 SPI 发 H7 之后自动 dump 一份
    - 'q' 关闭连续镜像(F407→H7 恢复 500fps)
    - 'p' 让 F407 立刻 printf 一帧 summary
    - 'r' 让 F407 额外发一份 one-shot binary(可以和 continuous 同帧,seq 区分)

用法:
    python3 ad9226_bin_watch.py                 # 自动找串口
    python3 ad9226_bin_watch.py /dev/ttyUSB0    # 指定串口
    python3 ad9226_bin_watch.py -b 115200 ...   # 指定 baud (默认 921600)
    python3 ad9226_bin_watch.py --record out.bin  # 录 raw 流到文件
    python3 ad9226_bin_watch.py --csv out.csv    # 解析后存 CSV

交互命令 (viewer 收到按键):
    q           → 发 'q' 给 F407 关闭连续镜像 (再按一次开)
    p           → 发 'p' 给 F407 打印一帧 summary
    r           → 发 'r' 给 F407 发一份 one-shot binary
    s           → 打印当前 viewer 缓存的最新一帧的 stats
    w           → 画最近一帧的波形 (要 matplotlib,没装就只打文字)
    h           → 画最近 N 帧的 seq/mean/pp 历史
    save <name> → 存最新一帧到 CSV
    help        → 帮助
    Ctrl-C      → 退出

依赖:
    pip3 install pyserial numpy
    pip3 install --user --break-system-packages matplotlib  # 可选
"""

import argparse
import glob
import struct
import sys
import threading
import time
from collections import deque
from datetime import datetime

import numpy as np

# matplotlib 可选
HAS_MPL = False
plt = None
try:
    import matplotlib
    try:
        matplotlib.use('TkAgg')
        import matplotlib.pyplot as _plt
        _plt.figure(); _plt.close()  # 验证后端
        plt = _plt; HAS_MPL = True
    except Exception:
        matplotlib.use('Agg')
        import matplotlib.pyplot as _plt
        plt = _plt; HAS_MPL = True
        print('[info] matplotlib 后端用 Agg,只存 PNG 不弹窗')
except ImportError:
    print('[info] matplotlib 未装, w/h 命令会提示')

import serial


# 协议常量
STX = b'\xAA\x55'
ETX = b'\x55\xAA'
HEADER_FMT = '<IIH'      # seq, fs_hz, n   (LE)
HEADER_LEN = struct.calcsize(HEADER_FMT)  # = 10
FOOTER_LEN = 2
STX_LEN = 2
MIN_FRAME = STX_LEN + HEADER_LEN + 2 + FOOTER_LEN  # n=2 最小帧
DEFAULT_N = 4096
DEFAULT_FRAME_LEN = STX_LEN + HEADER_LEN + DEFAULT_N * 2 + FOOTER_LEN  # 8206

# 零点和 scale,和 F407 标定一致
ZERO_CODE = 2055
SCALE_UV_PER_CODE = 5236  # uV per code
# AD9226 是 12-bit ADC,数据落在 s_adc_buf[i] 的低 12 位
# 高 4 位是 PF12-PF15 的电平(被 DMA 一起读到 IDR 里了,不是 ADC 数据)
# 必须在 stats 之前 mask
ADC_MASK = 0x0FFF


def find_port():
    ports = sorted(glob.glob('/dev/ttyUSB*') + glob.glob('/dev/ttyACM*'))
    if not ports:
        raise RuntimeError('找不到 /dev/ttyUSB* 或 /dev/ttyACM*')
    if len(ports) > 1:
        print(f'发现多个串口: {ports}, 用第一个')
    return ports[0]


class FrameParser:
    """增量解析 binary 流,容忍半包、错位、重同步"""
    def __init__(self, verbose=False):
        self.buf = bytearray()
        self.frames = deque(maxlen=64)  # 最近 64 帧
        self.errors = {'bad_stx': 0, 'bad_etx': 0, 'short': 0, 'n_mismatch': 0}
        self.total_bytes = 0
        self.frames_ok = 0
        self.last_seq = None
        self.lock = threading.Lock()
        self.verbose = verbose

    def feed(self, data: bytes):
        with self.lock:
            self.buf.extend(data)
            self.total_bytes += len(data)
            self._consume()

    def _consume(self):
        """在 self.buf 上尝试切出完整帧,直到数据不够为止"""
        while True:
            # 找 STX
            stx_idx = self.buf.find(STX)
            if stx_idx < 0:
                # 没有 STX, 留最后 1 字节 (避免 STX 被切两半)
                if len(self.buf) > 1:
                    del self.buf[:-1]
                return
            if stx_idx > 0:
                # STX 之前有杂数据 (例如命令行 printf "[DUMP] continuous = 1\r\n")
                # 砍掉,但记录
                if self.verbose:
                    sys.stderr.write(f'[parser] skip {stx_idx} junk bytes: {bytes(self.buf[:stx_idx])!r}\n')
                del self.buf[:stx_idx]

            # 需要 STX + header
            if len(self.buf) < STX_LEN + HEADER_LEN:
                return
            seq, fs_hz, n = struct.unpack_from(HEADER_FMT, self.buf, STX_LEN)
            frame_len = STX_LEN + HEADER_LEN + n * 2 + FOOTER_LEN
            if n < 2 or n > 8192:
                # n 异常,说明 STX 找错位置了,跳一个字节重找
                self.errors['n_mismatch'] += 1
                del self.buf[:STX_LEN]
                continue
            if len(self.buf) < frame_len:
                return  # 等更多数据
            # 校验 ETX
            if self.buf[frame_len - FOOTER_LEN:frame_len] != ETX:
                self.errors['bad_etx'] += 1
                if self.verbose:
                    sys.stderr.write(f'[parser] bad ETX at seq={seq}, skip 1 byte\n')
                del self.buf[:STX_LEN]
                continue
            # 解析 codes
            codes_offset = STX_LEN + HEADER_LEN
            try:
                codes = np.frombuffer(
                    self.buf[codes_offset:codes_offset + n * 2],
                    dtype='<u2',
                ).copy()
            except Exception as e:
                self.errors['short'] += 1
                del self.buf[:STX_LEN]
                continue

            # 入队
            self.frames.append({
                'ts': time.time(),
                'seq': seq,
                'fs_hz': fs_hz,
                'n': n,
                'codes': codes,
            })
            self.frames_ok += 1
            self.last_seq = seq
            del self.buf[:frame_len]

    def latest(self):
        with self.lock:
            if not self.frames:
                return None
            return self.frames[-1].copy()

    def stats(self):
        with self.lock:
            return {
                'total_bytes': self.total_bytes,
                'frames_ok': self.frames_ok,
                'last_seq': self.last_seq,
                'errors': dict(self.errors),
                'frames_in_buf': len(self.frames),
            }


def reader_thread(ser: serial.Serial, parser: FrameParser, record_fh, stop_evt: threading.Event):
    """后台读串口 → parser.feed;同时写到 record_fh(若指定)"""
    ser.timeout = 0.5  # 短超时, 周期性检查 stop
    while not stop_evt.is_set():
        try:
            chunk = ser.read(4096)
        except Exception as e:
            sys.stderr.write(f'[reader] {e}\n')
            break
        if not chunk:
            continue
        if record_fh is not None:
            record_fh.write(chunk)
            record_fh.flush()
        parser.feed(chunk)


def frame_stats(arr: np.ndarray):
    """numpy 算 min/max/mean/std/pp (和 F407 端 frame_stats 一致)
    关键: 必须先 & 0x0FFF,丢掉 PF12-PF15 噪声(高 4 bit)
    """
    if arr is None or len(arr) == 0:
        return None
    arr12 = arr & ADC_MASK  # 12-bit ADC code
    return {
        'min': int(arr12.min()),
        'max': int(arr12.max()),
        'mean': float(arr12.mean()),
        'pp': int(arr12.max() - arr12.min()),
        'std': float(arr12.std()),
        # 保留 raw 16-bit 值,排查时可以 print
        'raw_mean': float(arr.mean()),
        'raw_min': int(arr.min()),
        'raw_max': int(arr.max()),
    }


def print_frame_summary(frame):
    if frame is None:
        print('(no frame yet)')
        return
    s = frame_stats(frame['codes'])
    print(f"=== seq={frame['seq']} fs={frame['fs_hz']} Hz n={frame['n']} ===")
    print(f"  12-bit ADC code (低 12 位, 高 4 位已 mask):")
    print(f"    min={s['min']:4d} max={s['max']:4d} "
          f"pp={s['pp']:4d} mean={s['mean']:.1f} std={s['std']:.2f}")
    print(f"  raw 16-bit IDR (含 PF12-PF15, 仅供排查):")
    print(f"    min=0x{s['raw_min']:04X} max=0x{s['raw_max']:04X} mean={s['raw_mean']:.1f}")
    # 物理量
    mean_uV = (s['mean'] - ZERO_CODE) * SCALE_UV_PER_CODE
    pp_uV = s['pp'] * SCALE_UV_PER_CODE
    print(f"  物理 (按 0V={ZERO_CODE}, {SCALE_UV_PER_CODE/1000:.3f} mV/code):")
    print(f"    mean = {mean_uV/1000:+.3f} V")
    print(f"    pp   = {pp_uV/1000:.3f} V ({pp_uV:.0f} uV)")


def save_frame_csv(frame, filename):
    if frame is None:
        print('(no frame to save)')
        return
    n = frame['n']
    idx = np.arange(n)
    # mask 12-bit,免得 PF12-PF15 把零点和码值都污染了
    codes12 = (frame['codes'] & ADC_MASK).astype(np.int64)
    uV = (codes12 - ZERO_CODE) * SCALE_UV_PER_CODE
    with open(filename, 'w') as f:
        f.write(f"# seq={frame['seq']} fs_hz={frame['fs_hz']} n={n} "
                f"(code 已 & 0x0FFF, 零点={ZERO_CODE}, {SCALE_UV_PER_CODE}uV/code)\n")
        f.write('# idx,code,uV\n')
        for i in range(n):
            f.write(f'{i},{codes12[i]},{uV[i]}\n')
    print(f'[saved] {filename} ({n} points, 12-bit masked)')


def plot_waveform(frame):
    if not HAS_MPL:
        print('[warn] matplotlib 没装, 无法画图, 改用文字直方图:')
        text_histogram(frame)
        return
    n = frame['n']
    # 关键: mask 12-bit, 免得 PF12-PF15 (高 4 bit) 把波形整条往上抬 26000 codes
    codes12 = (frame['codes'] & ADC_MASK).astype(np.float64)
    uV = (codes12 - ZERO_CODE) * SCALE_UV_PER_CODE / 1000.0  # mV
    t = np.arange(n) / frame['fs_hz'] * 1000.0  # ms
    fig, ax = plt.subplots(figsize=(10, 4))
    ax.plot(t, uV, linewidth=0.6)
    ax.set_xlabel('time (ms)')
    ax.set_ylabel('voltage (mV)')
    ax.set_title(f"seq={frame['seq']} fs={frame['fs_hz']} Hz n={n} (12-bit masked)")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    png = f'ad9226_wave_{datetime.now().strftime("%Y%m%d_%H%M%S")}_seq{frame["seq"]}.png'
    fig.savefig(png, dpi=150)
    print(f'[saved] {png}')


def plot_history(parser: FrameParser):
    if not HAS_MPL:
        print('[warn] matplotlib 没装')
        return
    with parser.lock:
        if len(parser.frames) < 2:
            print('(frames < 2)')
            return
        seqs = [f['seq'] for f in parser.frames]
        # 关键: 每个 frame 的 codes 都要 mask 12-bit, 否则 mean 会卡在 26000
        means = [float((f['codes'] & ADC_MASK).mean()) for f in parser.frames]
        pps = [int((f['codes'] & ADC_MASK).max() - (f['codes'] & ADC_MASK).min())
               for f in parser.frames]
    fig, axes = plt.subplots(2, 1, figsize=(10, 5), sharex=True)
    axes[0].plot(seqs, means, 'b-o', markersize=3)
    axes[0].axhline(ZERO_CODE, color='gray', linestyle='--', label=f'zero ({ZERO_CODE})')
    axes[0].set_ylabel('mean code (12-bit)'); axes[0].legend(); axes[0].grid(True, alpha=0.3)
    axes[1].plot(seqs, pps, 'r-o', markersize=3)
    axes[1].set_ylabel('pp code'); axes[1].set_xlabel('seq')
    axes[1].grid(True, alpha=0.3)
    fig.tight_layout()
    png = f'ad9226_history_{datetime.now().strftime("%Y%m%d_%H%M%S")}.png'
    fig.savefig(png, dpi=150)
    print(f'[saved] {png}')


def text_histogram(frame, width=60):
    if frame is None or len(frame['codes']) == 0:
        return
    codes = frame['codes'] & ADC_MASK  # mask 12-bit
    lo, hi = int(codes.min()), int(codes.max())
    if lo == hi:
        hi = lo + 1
    bins = np.linspace(lo, hi, width + 1)
    hist, _ = np.histogram(codes, bins=bins)
    max_h = hist.max() or 1
    print(f'  code 分布 [{lo}, {hi}], 共 {len(codes)} 点:')
    for i, h in enumerate(hist):
        bar = '#' * int(40 * h / max_h)
        print(f'  {int(bins[i]):4d}|{bar}')


def main():
    ap = argparse.ArgumentParser(description='AD9226 binary mirror viewer (F407→H7 SPI 镜像)')
    ap.add_argument('port', nargs='?', help='串口 (默认自动找)')
    ap.add_argument('-b', '--baud', type=int, default=921600, help='baud (默认 921600)')
    ap.add_argument('--record', metavar='FILE', help='录 raw binary 流到 FILE')
    ap.add_argument('--csv', metavar='FILE', help='收到首帧后存 CSV 到 FILE')
    ap.add_argument('-v', '--verbose', action='store_true', help='打印解析错误详情')
    args = ap.parse_args()

    port = args.port or find_port()
    print(f'打开 {port} @ {args.baud} 8N1 ...')
    ser = serial.Serial(port, args.baud, bytesize=8, parity='N', stopbits=1, timeout=0.5)
    time.sleep(0.3)  # 让 F407 上电/复位后稳定
    ser.reset_input_buffer()
    print('已连, F407 端默认开 continuous binary 镜像 (921600 baud)')
    print("按 'h' 看命令帮助, Ctrl-C 退出")

    record_fh = open(args.record, 'wb') if args.record else None
    if record_fh:
        print(f'[recording] raw → {args.record}')

    parser = FrameParser(verbose=args.verbose)
    stop_evt = threading.Event()
    t = threading.Thread(target=reader_thread, args=(ser, parser, record_fh, stop_evt), daemon=True)
    t.start()

    csv_saved = False
    last_print = 0.0
    try:
        while True:
            time.sleep(0.1)
            # 每秒打印一次状态,让用户知道在收
            now = time.time()
            if now - last_print > 1.0:
                last_print = now
                st = parser.stats()
                f = parser.latest()
                fps = st['frames_ok'] / max(1e-3, now - (f['ts'] - st['total_bytes']/args.baud/10) if f else 1.0)
                # 简化的 fps:看 frames_ok/运行时间
                if f:
                    print(f'\r[live] seq={f["seq"]} n={f["n"]} buf={st["frames_in_buf"]} '
                          f'ok={st["frames_ok"]} err={sum(st["errors"].values())}    ',
                          end='', flush=True)
                else:
                    print(f'\r[live] waiting for first frame...    ', end='', flush=True)

            # 交互 (非阻塞)
            # 我们用 select 检查 stdin,避免 getch 卡住
            import select
            r, _, _ = select.select([sys.stdin], [], [], 0.05)
            if not r:
                continue
            line = sys.stdin.readline().strip().lower()
            if not line:
                continue
            cmd, *rest = line.split(maxsplit=1)
            arg = rest[0] if rest else ''
            if cmd in ('q', 'quit', 'exit'):
                # 发 'q' 关闭 F407 continuous dump,F407→H7 恢复 500 fps
                ser.write(b'q')
                print('\n[tx] q → F407 (toggle continuous dump)')
            elif cmd == 'p':
                ser.write(b'p')
                print('\n[tx] p → F407 (print one frame summary)')
            elif cmd == 'r':
                ser.write(b'r')
                print('\n[tx] r → F407 (one-shot binary dump)')
            elif cmd == 's':
                print_frame_summary(parser.latest())
            elif cmd == 'w':
                plot_waveform(parser.latest())
            elif cmd == 'h':
                plot_history(parser)
            elif cmd == 'save':
                fn = arg or f'ad9226_frame_{datetime.now().strftime("%Y%m%d_%H%M%S")}.csv'
                save_frame_csv(parser.latest(), fn)
            elif cmd in ('?', 'help'):
                print(__doc__)
            else:
                print(f'未知命令: {line!r} (输入 help)')
    except KeyboardInterrupt:
        print('\n退出...')
    finally:
        stop_evt.set()
        t.join(timeout=1.5)
        if record_fh:
            record_fh.close()
        ser.close()
        st = parser.stats()
        print(f'\n[final] total_bytes={st["total_bytes"]} frames_ok={st["frames_ok"]} '
              f'errors={st["errors"]}')


if __name__ == '__main__':
    main()
