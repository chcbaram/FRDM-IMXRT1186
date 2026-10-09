#!/usr/bin/env python3
"""
images/*.svg 의 텍스트 레이아웃을 검사한다.

이 저장소의 다이어그램은 전부 손으로 쓴 SVG 다. 브라우저에서 열어보기 전에는
글자가 박스를 넘거나 서로 겹치는 것을 알기 어려워서, 폭을 추정해 미리 잡는다.

    python3 firmware/docs/check_svg.py

검사 항목
  1. viewBox 밖으로 나가는 텍스트
  2. 같은 줄에서 서로 겹치는 텍스트
  3. 자기가 들어 있는 <rect> 밖으로 삐져나오는 텍스트

한글은 폭이 영문의 약 두 배라 글자 종류별로 다르게 센다. 추정이므로 오차가
있지만, 실제로 깨진 것은 대부분 여유 있게 넘어가서 걸린다.
"""
import re
import sys
from pathlib import Path

# 클래스별 기본 font-size. <style> 에 정의돼 있으면 그 값이 우선한다.
DEFAULT_FONT_SIZE = {
    'm': 11.5, 's': 11.5, 'b': 13.5, 'ttl': 15.0,
    'lbl': 12.0, 'step': 12.0, 'addr': 12.0, 'name': 14.0, 'sub': 11.5,
}
BASELINE_TOL = 0.7  # 같은 줄로 볼 baseline 차이 (font-size 배수)
BOX_PAD = 4.0       # 박스 오른쪽 안쪽 여유


def text_width(s: str, font_size: float) -> float:
    total = 0.0
    for ch in s:
        if '가' <= ch <= '힣' or 'ㄱ' <= ch <= 'ㆎ':
            total += font_size                 # 한글은 전각
        elif ch in ' .,·|':
            total += font_size * 0.32
        elif ch.isupper() or ch in 'mwMW—':
            total += font_size * 0.68
        else:
            total += font_size * 0.56
    return total


def parse(path: Path):
    src = path.read_text()

    vb = re.search(r'viewBox="0 0 (\d+) (\d+)"', src)
    width, height = int(vb.group(1)), int(vb.group(2))

    font_size = dict(DEFAULT_FONT_SIZE)
    for cls, body in re.findall(r'\.(\w+)\s*\{([^}]*)\}', src):
        m = re.search(r'font-size:\s*([\d.]+)px', body)
        if m:
            font_size[cls] = float(m.group(1))

    rects = [tuple(map(float, m.groups())) for m in re.finditer(
        r'<rect x="([\d.-]+)" y="([\d.-]+)" width="([\d.]+)" height="([\d.]+)"', src)]

    texts = []
    for m in re.finditer(r'<text\b([^>]*)>(.*?)</text>', src, re.S):
        attrs, inner = m.group(1), m.group(2)
        xm = re.search(r'\bx="([\d.-]+)"', attrs)
        ym = re.search(r'\by="([\d.-]+)"', attrs)
        if not xm or not ym:
            continue

        body = re.sub(r'<[^>]+>', '', inner).strip()
        if not body:
            continue

        cls = re.search(r'class="(\w+)"', attrs)
        explicit = re.search(r'font-size="([\d.]+)"', attrs)
        size = (float(explicit.group(1)) if explicit
                else font_size.get(cls.group(1) if cls else 's', 11.5))

        x, y = float(xm.group(1)), float(ym.group(1))
        w = text_width(body, size)

        if 'text-anchor="end"' in attrs:
            x0 = x - w
        elif 'text-anchor="middle"' in attrs:
            x0 = x - w / 2
        else:
            x0 = x

        texts.append({'x0': x0, 'x1': x0 + w, 'y': y, 'size': size, 'text': body})

    return width, height, rects, texts


def check(path: Path):
    width, height, rects, texts = parse(path)
    problems = []

    for t in texts:
        if t['x1'] > width - 2 or t['x0'] < -2 or t['y'] > height - 2:
            problems.append(f"이탈  [{t['x0']:.0f}~{t['x1']:.0f}] y={t['y']:.0f}  {t['text'][:46]}")

    for i, a in enumerate(texts):
        for b in texts[i + 1:]:
            if abs(a['y'] - b['y']) > max(a['size'], b['size']) * BASELINE_TOL:
                continue
            overlap = min(a['x1'], b['x1']) - max(a['x0'], b['x0'])
            if overlap > 1.5:
                problems.append(
                    f"겹침  {overlap:.0f}px y={a['y']:.0f}  '{a['text'][:26]}' × '{b['text'][:26]}'")

    for t in texts:
        # 텍스트 시작점을 담는 가장 작은 rect 를 그 텍스트의 컨테이너로 본다
        holders = [r for r in rects
                   if r[0] <= t['x0'] <= r[0] + r[2] and r[1] <= t['y'] <= r[1] + r[3]]
        if not holders:
            continue
        rx, ry, rw, rh = min(holders, key=lambda r: r[2] * r[3])
        if t['x1'] > rx + rw - BOX_PAD:
            problems.append(
                f"박스밖 {t['x1'] - (rx + rw):.0f}px  y={t['y']:.0f}  {t['text'][:40]}")

    return problems


def main() -> int:
    images = sorted((Path(__file__).parent / 'images').glob('*.svg'))
    total = 0

    for path in images:
        problems = check(path)
        total += len(problems)
        mark = 'OK' if not problems else f"{len(problems)}건"
        print(f"{path.name:24s} {mark}")
        for p in problems:
            print(f"    {p}")

    print(f"\n{len(images)}개 파일, 문제 {total}건")
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main())
