#!/bin/sh
# Desk Gateway 本地控制入口。
# GoatRemote 和 Karabiner 共用此脚本；旋钮事件使用短租约 jog 接口。

set -eu

DESK_BASE_URL='http://192.168.21.90'
DESK_KEY='desk-gateway'

case "${1:-}" in
    1|4)
        endpoint="/api/v1/desk/preset/$1/goto"
        ;;
    up|down)
        endpoint="/api/v1/desk/jog/$1"
        ;;
    stop)
        endpoint='/api/v1/desk/stop'
        ;;
    *)
        echo "用法: $0 1|4|up|down|stop" >&2
        exit 64
        ;;
esac

/usr/bin/curl \
    --fail \
    --silent \
    --show-error \
    --connect-timeout 2 \
    --max-time 5 \
    --request POST \
    --header "X-Desk-Key: ${DESK_KEY}" \
    "${DESK_BASE_URL}${endpoint}"
printf '\n'
