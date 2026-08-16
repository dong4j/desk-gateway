/** Desk Gateway 插件 UUID 与固定 REST 动作。 */
export const PLUGIN_UUID = "com.ulanzi.ulanzistudio.deskgateway";

export const ACTIONS = Object.freeze({
  sit: Object.freeze({
    uuid: `${PLUGIN_UUID}.sit`,
    label: "请坐",
    accent: "#35A7FF",
  }),
  stand: Object.freeze({
    uuid: `${PLUGIN_UUID}.stand`,
    label: "站立",
    accent: "#42D392",
  }),
  pomodoro: Object.freeze({
    uuid: `${PLUGIN_UUID}.pomodoro`,
    label: "番茄时刻",
    accent: "#FF6257",
  }),
});

export const ACTION_KIND_BY_UUID = Object.freeze(
  Object.fromEntries(Object.entries(ACTIONS).map(([kind, action]) => [action.uuid, kind])),
);

export const DEFAULT_CONFIG = Object.freeze({
  gateway_url: "http://desk-gateway.local",
  api_key: "",
});

