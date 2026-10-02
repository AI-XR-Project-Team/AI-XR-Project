#!/usr/bin/env python3
"""ARCore Cloud Anchor Management API 관리 CLI (10단계 TTL 프로브/운영).

핵심 로직은 app/services/cloud_anchor_admin.py 에 있고, 이 파일은 얇은 CLI 래퍼다.
판정 기준·정지 지점은 docs/nav-stage10-ttl-probe-runbook.md 참조.

사용 (backend_server/ 를 작업 디렉터리로):
    export SA_KEY_PATH=/Users/lim/arcore-sa-key.json
    python -m scripts.arcore_anchor_admin get <cloud_anchor_id>
    python -m scripts.arcore_anchor_admin extend <cloud_anchor_id>
    python -m scripts.arcore_anchor_admin expire-in <cloud_anchor_id> <분>

의존: google-auth, requests  (pip install --break-system-packages google-auth requests)
"""
import argparse
import datetime as _dt
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.services.cloud_anchor_admin import AnchorAdminClient  # noqa: E402


def _show(info):
    print(json.dumps(info.raw, indent=2, ensure_ascii=False))
    if info.create_time and info.max_expire_time:
        span = info.max_expire_time - info.create_time
        print(f"\n>>> maximumExpireTime - createTime = {span}  "
              f"({span.days}일 {span.seconds // 3600}시간)")
    if info.create_time and info.expire_time:
        print(f">>> expireTime    - createTime = {info.expire_time - info.create_time}")


def cmd_get(client, args):
    status, info, txt = client.get(args.anchor_id)
    print(f"HTTP {status}")
    if status == 200:
        _show(info)
    else:
        print(txt)
        if status == 404:
            print("\n(404 = 인증 OK, 해당 앵커만 없음)")
        sys.exit(0 if status == 404 else 1)


def cmd_extend(client, args):
    status, info, txt = client.extend_to_max(args.anchor_id)
    print(f"PATCH HTTP {status}")
    if status == 200:
        print("\n=== 연장 후 상태 ===")
        _show(info)
    else:
        print(txt)
        sys.exit(1)


def cmd_expire_in(client, args):
    when = _dt.datetime.now(_dt.timezone.utc) + _dt.timedelta(minutes=args.minutes)
    status, info, txt = client.set_expire(args.anchor_id, when)
    print(f"expireTime <- 지금+{args.minutes}분\nPATCH HTTP {status}")
    if status == 200:
        _show(info)
    else:
        print(txt)
        print("\n(거부됨 — 조기 만료 불가. 자연 만료를 기다린다.)")
        sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description="ARCore Cloud Anchor Management API CLI")
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("get", help="앵커 조회")
    g.add_argument("anchor_id")
    e = sub.add_parser("extend", help="expireTime 을 maximumExpireTime 까지 연장")
    e.add_argument("anchor_id")
    x = sub.add_parser("expire-in", help="expireTime 을 N분 뒤로 (대조군 조기 만료용)")
    x.add_argument("anchor_id")
    x.add_argument("minutes", type=int)
    args = ap.parse_args()

    client = AnchorAdminClient()
    {"get": cmd_get, "extend": cmd_extend, "expire-in": cmd_expire_in}[args.cmd](client, args)


if __name__ == "__main__":
    main()
