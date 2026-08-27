/**
 * @file desk_motion_audio.c
 * @brief 非阻塞运动语义提示、档位完成语音和接近上限的变频提示音。
 *
 * desk_core 回调只向本组件队列复制一个小事件；高度轮询、节奏判断和 I2S
 * 播放全部在低优先级任务中完成，不能给旋钮、REST、BLE 或面板增加等待。
 */
#include "desk_motion_audio.h"

#include "desk_audio.h"
#include "desk_core.h"
#include "desk_motion_audio_logic.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdatomic.h>

#define MOTION_AUDIO_EVENT_QUEUE_DEPTH 16U
#define MOTION_AUDIO_POLL_MS 100U
#define MOTION_AUDIO_ARMED_GRACE_MS 1200U
#define MOTION_AUDIO_TARGET_TOLERANCE_MM 10
#define MOTION_AUDIO_ALERT_REPEAT_MS 3000U

typedef struct {
    bool valid;
    desk_audio_prompt_t prompt;
    desk_audio_priority_t priority;
} pending_prompt_t;

typedef struct {
    desk_core_motion_action_t action;
    int target_height_mm;
    uint32_t request_ms;
    uint32_t last_chime_ms;
    desk_status_t previous_status;
    bool start_announced;
    bool motion_seen;
    bool was_upward;
    bool rejected_pending;
    desk_core_event_t rejected_event;
    bool last_alert_valid;
    desk_audio_prompt_t last_alert;
    uint32_t last_alert_ms;
    pending_prompt_t pending;
} motion_audio_state_t;

static const char *TAG = "motion_audio";
static QueueHandle_t s_events;
static TaskHandle_t s_task;
static atomic_uint s_dropped_events;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool status_is_motion(desk_status_t status)
{
    return status == DESK_STATUS_MOVING_UP ||
           status == DESK_STATUS_MOVING_DOWN ||
           status == DESK_STATUS_GOTO_PRESET;
}

static bool action_is_target(desk_core_motion_action_t action)
{
    return action == DESK_CORE_MOTION_RAISE_TO_MAX ||
           action == DESK_CORE_MOTION_PRESET_1 ||
           action == DESK_CORE_MOTION_PRESET_4 ||
           action == DESK_CORE_MOTION_HEIGHT_PRESET;
}

static bool action_matches_status(desk_core_motion_action_t action,
                                  desk_status_t status)
{
    switch (action) {
    case DESK_CORE_MOTION_HOLD_UP:
    case DESK_CORE_MOTION_JOG_UP:
        return status == DESK_STATUS_MOVING_UP;
    case DESK_CORE_MOTION_HOLD_DOWN:
    case DESK_CORE_MOTION_JOG_DOWN:
        return status == DESK_STATUS_MOVING_DOWN;
    case DESK_CORE_MOTION_RAISE_TO_MAX:
        return status == DESK_STATUS_MOVING_UP ||
               status == DESK_STATUS_GOTO_PRESET;
    case DESK_CORE_MOTION_PRESET_1:
    case DESK_CORE_MOTION_PRESET_4:
    case DESK_CORE_MOTION_HEIGHT_PRESET:
        return status == DESK_STATUS_GOTO_PRESET;
    case DESK_CORE_MOTION_NONE:
    default:
        return false;
    }
}

static bool action_moves_up(const motion_audio_state_t *state,
                            const desk_core_snapshot_t *snapshot)
{
    if (snapshot->status == DESK_STATUS_MOVING_UP) {
        return true;
    }
    if (snapshot->status != DESK_STATUS_GOTO_PRESET) {
        return false;
    }
    if (state->action == DESK_CORE_MOTION_RAISE_TO_MAX) {
        return true;
    }
    return snapshot->height_known && state->target_height_mm >= 0 &&
           snapshot->height_mm < state->target_height_mm;
}

static bool request_wants_up(const desk_core_event_t *event,
                             const desk_core_snapshot_t *snapshot)
{
    switch (event->motion) {
    case DESK_CORE_MOTION_HOLD_UP:
    case DESK_CORE_MOTION_JOG_UP:
    case DESK_CORE_MOTION_RAISE_TO_MAX:
        return true;
    case DESK_CORE_MOTION_HOLD_DOWN:
    case DESK_CORE_MOTION_JOG_DOWN:
        return false;
    case DESK_CORE_MOTION_PRESET_1:
    case DESK_CORE_MOTION_PRESET_4:
    case DESK_CORE_MOTION_HEIGHT_PRESET:
        return !snapshot->height_known || event->target_height_mm < 0 ||
               snapshot->height_mm < event->target_height_mm;
    case DESK_CORE_MOTION_NONE:
    default:
        return false;
    }
}

static bool start_prompt_for(desk_core_motion_action_t action,
                             desk_audio_prompt_t *out)
{
    switch (action) {
    case DESK_CORE_MOTION_HOLD_UP:
    case DESK_CORE_MOTION_JOG_UP:
        *out = DESK_AUDIO_PROMPT_MOTION_UP;
        return true;
    case DESK_CORE_MOTION_HOLD_DOWN:
    case DESK_CORE_MOTION_JOG_DOWN:
        *out = DESK_AUDIO_PROMPT_MOTION_DOWN;
        return true;
    case DESK_CORE_MOTION_RAISE_TO_MAX:
        *out = DESK_AUDIO_PROMPT_RAISE_TO_MAX_START;
        return true;
    case DESK_CORE_MOTION_PRESET_1:
        *out = DESK_AUDIO_PROMPT_PRESET1_START;
        return true;
    case DESK_CORE_MOTION_PRESET_4:
        *out = DESK_AUDIO_PROMPT_PRESET4_START;
        return true;
    case DESK_CORE_MOTION_HEIGHT_PRESET:
        *out = DESK_AUDIO_PROMPT_HEIGHT_PRESET_START;
        return true;
    case DESK_CORE_MOTION_NONE:
    default:
        return false;
    }
}

static bool reached_prompt_for(desk_core_motion_action_t action,
                               desk_audio_prompt_t *out)
{
    switch (action) {
    case DESK_CORE_MOTION_RAISE_TO_MAX:
        *out = DESK_AUDIO_PROMPT_MAX_HEIGHT_REACHED;
        return true;
    case DESK_CORE_MOTION_PRESET_1:
        *out = DESK_AUDIO_PROMPT_PRESET1_REACHED;
        return true;
    case DESK_CORE_MOTION_PRESET_4:
        *out = DESK_AUDIO_PROMPT_PRESET4_REACHED;
        return true;
    case DESK_CORE_MOTION_HEIGHT_PRESET:
        *out = DESK_AUDIO_PROMPT_HEIGHT_PRESET_REACHED;
        return true;
    default:
        return false;
    }
}

static bool blocked_prompt_for(desk_upward_block_reason_t reason,
                               desk_audio_prompt_t *out)
{
    switch (reason) {
    case DESK_UPWARD_BLOCK_MAX_HEIGHT:
        *out = DESK_AUDIO_PROMPT_MAX_HEIGHT_REACHED;
        return true;
    case DESK_UPWARD_BLOCK_RIGHT_GAP:
        *out = DESK_AUDIO_PROMPT_RIGHT_GAP_BLOCKED;
        return true;
    case DESK_UPWARD_BLOCK_HEIGHT_UNKNOWN:
    case DESK_UPWARD_BLOCK_RIGHT_GAP_UNKNOWN:
        *out = DESK_AUDIO_PROMPT_SENSOR_UNAVAILABLE;
        return true;
    case DESK_UPWARD_BLOCK_NONE:
    case DESK_UPWARD_BLOCK_OTHER:
    default:
        return false;
    }
}

static void queue_prompt(motion_audio_state_t *state,
                         desk_audio_prompt_t prompt,
                         desk_audio_priority_t priority)
{
    if (!state->pending.valid || priority >= state->pending.priority) {
        state->pending = (pending_prompt_t){
            .valid = true,
            .prompt = prompt,
            .priority = priority,
        };
    }
}

static void queue_alert(motion_audio_state_t *state,
                        desk_audio_prompt_t prompt, uint32_t current_ms)
{
    if (state->last_alert_valid && state->last_alert == prompt &&
        (uint32_t)(current_ms - state->last_alert_ms) <
            MOTION_AUDIO_ALERT_REPEAT_MS) {
        return;
    }
    state->last_alert_valid = true;
    state->last_alert = prompt;
    state->last_alert_ms = current_ms;
    queue_prompt(state, prompt, DESK_AUDIO_PRIORITY_ALARM);
}

static void queue_block_alert(motion_audio_state_t *state,
                              const desk_core_snapshot_t *snapshot,
                              uint32_t current_ms)
{
    desk_audio_prompt_t prompt;
    if (blocked_prompt_for(snapshot->upward_block_reason, &prompt)) {
        queue_alert(state, prompt, current_ms);
    }
}

static void clear_motion(motion_audio_state_t *state)
{
    state->action = DESK_CORE_MOTION_NONE;
    state->target_height_mm = -1;
    state->start_announced = false;
    state->motion_seen = false;
    state->was_upward = false;
}

static void handle_event(motion_audio_state_t *state,
                         const desk_core_event_t *event,
                         uint32_t current_ms)
{
    switch (event->kind) {
    case DESK_CORE_EVENT_MOTION_ACCEPTED:
        if (event->motion == DESK_CORE_MOTION_NONE) {
            return;
        }
        if (state->action != event->motion) {
            clear_motion(state);
            state->action = event->motion;
            state->last_chime_ms = current_ms;
        }
        state->target_height_mm = event->target_height_mm;
        state->request_ms = current_ms;
        break;
    case DESK_CORE_EVENT_MOTION_REJECTED:
        state->rejected_event = *event;
        state->rejected_pending = true;
        break;
    case DESK_CORE_EVENT_PRESET_SAVED:
        queue_prompt(state, DESK_AUDIO_PROMPT_PRESET_SAVED,
                     DESK_AUDIO_PRIORITY_PREVIEW);
        break;
    case DESK_CORE_EVENT_CHILD_LOCK_CHANGED:
        if (event->enabled) {
            queue_alert(state, DESK_AUDIO_PROMPT_CHILD_LOCK_ENABLED,
                        current_ms);
        } else {
            queue_prompt(state, DESK_AUDIO_PROMPT_CHILD_LOCK_DISABLED,
                         DESK_AUDIO_PRIORITY_PREVIEW);
        }
        break;
    case DESK_CORE_EVENT_CONTROLLER_RESET_STARTED:
        queue_prompt(state, DESK_AUDIO_PROMPT_CONTROLLER_RESET_STARTED,
                     DESK_AUDIO_PRIORITY_PREVIEW);
        break;
    case DESK_CORE_EVENT_CONTROLLER_RESET_COMPLETED:
        queue_prompt(state, DESK_AUDIO_PROMPT_CONTROLLER_RESET_COMPLETED,
                     DESK_AUDIO_PRIORITY_PREVIEW);
        break;
    case DESK_CORE_EVENT_STOP_ACCEPTED:
    default:
        break;
    }
}

static void process_rejection(motion_audio_state_t *state,
                              const desk_core_snapshot_t *snapshot,
                              uint32_t current_ms)
{
    if (!state->rejected_pending) {
        return;
    }
    state->rejected_pending = false;
    if (snapshot->child_lock) {
        queue_alert(state, DESK_AUDIO_PROMPT_CHILD_LOCK_ENABLED,
                    current_ms);
        return;
    }
    if (request_wants_up(&state->rejected_event, snapshot)) {
        queue_block_alert(state, snapshot, current_ms);
    } else if (!snapshot->height_known &&
               action_is_target(state->rejected_event.motion)) {
        queue_alert(state, DESK_AUDIO_PROMPT_SENSOR_UNAVAILABLE,
                    current_ms);
    }
}

static void flush_pending_prompt(motion_audio_state_t *state)
{
    if (!state->pending.valid) {
        return;
    }
    desk_audio_snapshot_t audio = desk_audio_snapshot();
    if (!audio.available || !audio.enabled || audio.volume_percent == 0) {
        state->pending.valid = false;
        return;
    }
    if (audio.playing &&
        state->pending.priority == DESK_AUDIO_PRIORITY_PREVIEW) {
        return;
    }
    if (desk_audio_play(state->pending.prompt,
                        state->pending.priority) == ESP_OK) {
        state->pending.valid = false;
    }
}

static void maybe_play_chime(motion_audio_state_t *state,
                             const desk_core_snapshot_t *snapshot,
                             bool moving_up, uint32_t current_ms)
{
    if (state->pending.valid) {
        return;
    }
    uint32_t interval_ms = desk_motion_audio_chime_interval_ms(
        moving_up, snapshot->height_known, snapshot->height_mm,
        snapshot->max_height_mm);
    if (interval_ms == 0 ||
        (uint32_t)(current_ms - state->last_chime_ms) < interval_ms) {
        return;
    }
    desk_audio_snapshot_t audio = desk_audio_snapshot();
    if (!audio.available || !audio.enabled || audio.playing ||
        audio.volume_percent == 0) {
        return;
    }
    if (desk_audio_play(DESK_AUDIO_PROMPT_ATTENTION_CHIME,
                        DESK_AUDIO_PRIORITY_PREVIEW) == ESP_OK) {
        state->last_chime_ms = current_ms;
    }
}

static void motion_audio_task(void *arg)
{
    (void)arg;
    motion_audio_state_t state = {
        .action = DESK_CORE_MOTION_NONE,
        .target_height_mm = -1,
        .previous_status = DESK_STATUS_IDLE,
    };
    while (true) {
        desk_core_event_t event;
        while (xQueueReceive(s_events, &event, 0) == pdTRUE) {
            handle_event(&state, &event, now_ms());
        }
        unsigned int dropped = atomic_exchange(&s_dropped_events, 0);
        if (dropped > 0) {
            ESP_LOGW(TAG, "dropped %u coalescible motion audio events",
                     dropped);
        }

        uint32_t current_ms = now_ms();
        desk_core_snapshot_t snapshot = desk_core_snapshot();
        process_rejection(&state, &snapshot, current_ms);

        bool moving_now = status_is_motion(snapshot.status);
        if (moving_now && !status_is_motion(state.previous_status) &&
            state.action == DESK_CORE_MOTION_NONE) {
            /* 面板等驱动内入口没有 core 请求事件，仍按真实状态只提示一次。 */
            state.action = snapshot.status == DESK_STATUS_MOVING_UP
                               ? DESK_CORE_MOTION_HOLD_UP
                           : snapshot.status == DESK_STATUS_MOVING_DOWN
                               ? DESK_CORE_MOTION_HOLD_DOWN
                               : DESK_CORE_MOTION_HEIGHT_PRESET;
            state.target_height_mm = -1;
            state.request_ms = current_ms;
            state.last_chime_ms = current_ms;
        }
        bool moving_up = action_moves_up(&state, &snapshot);
        if (moving_now && action_matches_status(state.action,
                                                snapshot.status)) {
            state.motion_seen = true;
            state.was_upward = moving_up;
            if (!state.start_announced) {
                desk_audio_prompt_t prompt;
                if (start_prompt_for(state.action, &prompt)) {
                    queue_prompt(&state, prompt,
                                 DESK_AUDIO_PRIORITY_PREVIEW);
                }
                state.start_announced = true;
            }
        }

        if (status_is_motion(state.previous_status) && !moving_now &&
            state.motion_seen) {
            if (state.was_upward && snapshot.upward_blocked) {
                queue_block_alert(&state, &snapshot, current_ms);
            } else if (action_is_target(state.action) &&
                       desk_motion_audio_target_reached(
                           snapshot.height_known, snapshot.height_mm,
                           state.target_height_mm,
                           MOTION_AUDIO_TARGET_TOLERANCE_MM)) {
                desk_audio_prompt_t prompt;
                if (reached_prompt_for(state.action, &prompt)) {
                    queue_prompt(&state, prompt,
                                 DESK_AUDIO_PRIORITY_PREVIEW);
                }
            }
            clear_motion(&state);
        } else if (!moving_now && !state.motion_seen &&
                   state.action != DESK_CORE_MOTION_NONE &&
                   (uint32_t)(current_ms - state.request_ms) >=
                       MOTION_AUDIO_ARMED_GRACE_MS) {
            if (action_is_target(state.action) &&
                desk_motion_audio_target_reached(
                    snapshot.height_known, snapshot.height_mm,
                    state.target_height_mm,
                    MOTION_AUDIO_TARGET_TOLERANCE_MM)) {
                desk_audio_prompt_t prompt;
                if (reached_prompt_for(state.action, &prompt)) {
                    queue_prompt(&state, prompt,
                                 DESK_AUDIO_PRIORITY_PREVIEW);
                }
            }
            clear_motion(&state);
        }

        state.previous_status = snapshot.status;
        flush_pending_prompt(&state);
        maybe_play_chime(&state, &snapshot, moving_up, current_ms);
        vTaskDelay(pdMS_TO_TICKS(MOTION_AUDIO_POLL_MS));
    }
}

/** desk_core 可能在任意任务通知；这里只做零等待队列复制。 */
static void core_event_listener(const desk_core_event_t *event, void *context)
{
    (void)context;
    if (!event || !s_events ||
        xQueueSend(s_events, event, 0) != pdTRUE) {
        atomic_fetch_add(&s_dropped_events, 1);
    }
}

esp_err_t desk_motion_audio_start(void)
{
    if (s_task) {
        return ESP_OK;
    }
    desk_audio_snapshot_t audio = desk_audio_snapshot();
    if (!audio.available) {
        return ESP_ERR_INVALID_STATE;
    }
    s_events = xQueueCreate(MOTION_AUDIO_EVENT_QUEUE_DEPTH,
                            sizeof(desk_core_event_t));
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = desk_core_add_event_listener(core_event_listener, NULL);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(motion_audio_task, "motion_audio", 4096, NULL,
                    tskIDLE_PRIORITY + 1, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "ready; ceiling chime=150/80/30 mm");
    return ESP_OK;
}
