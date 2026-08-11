#!/bin/sh
# Desk Gateway 本地控制入口。
# GoatRemote 和 Karabiner 共用此脚本；旋钮事件由单一后台进程续租，
# 避免每个刻度单独启动 curl 造成事件间隔超过固件保护时间。

set -eu

DESK_BASE_URL='http://192.168.21.65'
DESK_KEY='desk-gateway'

JOG_STATE_DIR="${TMPDIR:-/tmp}/desk-gateway-jog-$(/usr/bin/id -u)"
JOG_LOCK_DIR="${JOG_STATE_DIR}/lock"
JOG_RENEW_SECONDS='0.15'
JOG_IDLE_TICKS=4

# 原子更新旋钮状态，避免 Karabiner 并发启动多个续期器。
acquire_jog_lock() {
    jog_lock_attempt=0
    /bin/mkdir -p "${JOG_STATE_DIR}"
    while ! /bin/mkdir "${JOG_LOCK_DIR}" 2>/dev/null; do
        jog_lock_attempt=$((jog_lock_attempt + 1))
        if [ "${jog_lock_attempt}" -ge 100 ]; then
            echo "无法获取旋钮状态锁" >&2
            return 1
        fi
        /bin/sleep 0.01
    done
}

release_jog_lock() {
    /bin/rmdir "${JOG_LOCK_DIR}" 2>/dev/null || true
}

read_jog_state() {
    JOG_STATE_SEQ=0
    JOG_STATE_DIRECTION='stop'
    if [ -f "${JOG_STATE_DIR}/seq" ]; then
        IFS= read -r JOG_STATE_SEQ < "${JOG_STATE_DIR}/seq" || JOG_STATE_SEQ=0
    fi
    if [ -f "${JOG_STATE_DIR}/direction" ]; then
        IFS= read -r JOG_STATE_DIRECTION < "${JOG_STATE_DIR}/direction" || JOG_STATE_DIRECTION='stop'
    fi
}

post_endpoint() {
    /usr/bin/curl \
        --fail \
        --silent \
        --show-error \
        --connect-timeout "${2}" \
        --max-time "${3}" \
        --request POST \
        --header "X-Desk-Key: ${DESK_KEY}" \
        "${DESK_BASE_URL}${1}"
}

jog_post_quietly() {
    # 本地网络阻塞时快速放弃本次续期，由固件 500 ms 租约安全停止。
    post_endpoint "${1}" '0.2' '0.4' >/dev/null 2>&1
}

remove_worker_pid_if_owned() {
    if [ -f "${JOG_STATE_DIR}/worker.pid" ]; then
        IFS= read -r jog_owner_pid < "${JOG_STATE_DIR}/worker.pid" || jog_owner_pid=''
        if [ "${jog_owner_pid}" = "$$" ]; then
            /bin/rm -f "${JOG_STATE_DIR}/worker.pid"
        fi
    fi
}

finish_jog_worker() {
    finish_with_stop="${1}"
    finish_seq="${2}"
    acquire_jog_lock || exit 75
    read_jog_state

    # 停止前再检查一次，避免将刚到达的新旋钮事件误停。
    if [ "${JOG_STATE_SEQ}" != "${finish_seq}" ]; then
        release_jog_lock
        return 1
    fi
    if [ "${finish_with_stop}" = 'yes' ]; then
        jog_post_quietly '/api/v1/desk/stop' || true
    fi
    remove_worker_pid_if_owned
    release_jog_lock
    return 0
}

jog_worker() {
    observed_seq="${1}"
    observed_direction="${2}"
    jog_active='no'
    idle_ticks=0

    while :; do
        /bin/sleep "${JOG_RENEW_SECONDS}"
        acquire_jog_lock || exit 75
        read_jog_state
        release_jog_lock

        if [ "${JOG_STATE_DIRECTION}" = 'stop' ]; then
            finish_jog_worker 'no' "${JOG_STATE_SEQ}" || continue
            return 0
        fi

        if [ "${JOG_STATE_SEQ}" != "${observed_seq}" ]; then
            if [ "${JOG_STATE_DIRECTION}" != "${observed_direction}" ]; then
                # 反向旋转先停止，新方向仍需要第二个物理刻度才启动。
                if [ "${jog_active}" = 'yes' ]; then
                    jog_post_quietly '/api/v1/desk/stop' || true
                fi
                jog_active='no'
                observed_direction="${JOG_STATE_DIRECTION}"
            elif [ "${jog_active}" = 'no' ]; then
                jog_active='yes'
            fi
            observed_seq="${JOG_STATE_SEQ}"
            idle_ticks=0
        else
            idle_ticks=$((idle_ticks + 1))
        fi

        if [ "${idle_ticks}" -ge "${JOG_IDLE_TICKS}" ]; then
            if [ "${jog_active}" = 'yes' ]; then
                finish_jog_worker 'yes' "${observed_seq}" || continue
            else
                finish_jog_worker 'no' "${observed_seq}" || continue
            fi
            return 0
        fi

        if [ "${jog_active}" = 'yes' ]; then
            jog_post_quietly "/api/v1/desk/jog/${observed_direction}" || true
        fi
    done
}

queue_jog_event() {
    jog_direction="${1}"
    acquire_jog_lock || exit 75
    read_jog_state
    next_seq=$((JOG_STATE_SEQ + 1))
    printf '%s\n' "${jog_direction}" > "${JOG_STATE_DIR}/direction"
    printf '%s\n' "${next_seq}" > "${JOG_STATE_DIR}/seq"

    worker_running='no'
    if [ -f "${JOG_STATE_DIR}/worker.pid" ]; then
        IFS= read -r worker_pid < "${JOG_STATE_DIR}/worker.pid" || worker_pid=''
        if [ -n "${worker_pid}" ] && /bin/kill -0 "${worker_pid}" 2>/dev/null; then
            worker_running='yes'
        fi
    fi
    if [ "${worker_running}" = 'no' ]; then
        /usr/bin/nohup "$0" __jog_worker "${next_seq}" "${jog_direction}" >/dev/null 2>&1 &
        printf '%s\n' "$!" > "${JOG_STATE_DIR}/worker.pid"
    fi
    release_jog_lock
}

signal_jog_stop() {
    acquire_jog_lock || exit 75
    read_jog_state
    printf '%s\n' 'stop' > "${JOG_STATE_DIR}/direction"
    printf '%s\n' "$((JOG_STATE_SEQ + 1))" > "${JOG_STATE_DIR}/seq"
    release_jog_lock
}

if [ "${1:-}" = '__jog_worker' ]; then
    jog_worker "${2}" "${3}"
    exit 0
fi

case "${1:-}" in
    1|4)
        signal_jog_stop
        post_endpoint '/api/v1/desk/stop' '2' '5' >/dev/null
        endpoint="/api/v1/desk/preset/$1/goto"
        ;;
    up|down)
        queue_jog_event "$1"
        exit 0
        ;;
    stop)
        signal_jog_stop
        endpoint='/api/v1/desk/stop'
        ;;
    *)
        echo "用法: $0 1|4|up|down|stop" >&2
        exit 64
        ;;
esac

post_endpoint "${endpoint}" '2' '5'
printf '\n'
