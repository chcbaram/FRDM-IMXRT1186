#!/usr/bin/env python3
"""문서의 상대 링크와 #앵커가 실제로 존재하는지 확인한다.

문서가 늘면서 파일을 옮기거나 제목을 고칠 때 링크가 조용히 죽는다.
브라우저로 눌러 보기 전에는 티가 안 나므로 검사로 잡는다.

    python3 firmware/docs/check_links.py

외부 링크(http/https)는 검사하지 않는다 — 네트워크에 의존하면 검사가 불안정해진다.
"""
import re
import sys
from pathlib import Path

INLINE = re.compile(r'\[([^\]]+)\]\(([^)]+)\)')
HEADING = re.compile(r'^(#{1,6})\s+(.*)$')
FENCE = re.compile(r'^\s*(```|~~~)')


def slug(title: str) -> str:
    """GitHub 의 앵커 생성 규칙.

    공백을 **하나씩** 하이픈으로 바꾼다. 연속 공백을 합치지 않는다 —
    "함정 — CPU1" 처럼 문장부호가 빠지면 하이픈이 둘 남는 게 정상이다.
    """
    t = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', title)   # 링크는 표시 문자만
    t = re.sub(r'`|\*\*|\*|~~', '', t)                   # 인라인 마크업 제거
    t = t.strip().lower()
    t = re.sub(r'[^\w\s-]', '', t, flags=re.UNICODE)     # 문장부호 제거
    return t.replace(' ', '-')


def anchors(text: str) -> set:
    """문서의 제목에서 앵커 목록을 만든다. 코드블록 안의 # 는 제목이 아니다."""
    out = set()
    in_fence = False
    for line in text.split('\n'):
        if FENCE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        m = HEADING.match(line)
        if m:
            out.add(slug(m.group(2)))
    return out


def links(text: str):
    """코드블록 밖의 인라인 링크만 돌려준다."""
    in_fence = False
    for no, line in enumerate(text.split('\n'), 1):
        if FENCE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        for label, target in INLINE.findall(line):
            yield no, label, target


def main() -> int:
    root = Path(__file__).resolve().parent.parent.parent
    files = sorted(p for p in root.rglob('*.md') if '.git' not in p.parts)
    cache: dict = {}
    problems = 0

    for md in files:
        text = md.read_text(encoding='utf-8')
        for no, label, target in links(text):
            if target.startswith(('http://', 'https://', 'mailto:')):
                continue

            path, _, frag = target.partition('#')
            dest = (md.parent / path).resolve() if path else md.resolve()
            rel = md.relative_to(root)

            if not dest.exists():
                print(f'  {rel}:{no}  없는 파일  [{label}]({target})')
                problems += 1
                continue

            if not frag or dest.suffix != '.md':
                continue

            if dest not in cache:
                cache[dest] = anchors(dest.read_text(encoding='utf-8'))
            if frag not in cache[dest]:
                print(f'  {rel}:{no}  없는 앵커  [{label}]({target})')
                problems += 1

    print(f'\n{len(files)} 개 문서, ', end='')
    print(f'{problems} 건의 문제' if problems else '깨진 링크 없음')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
