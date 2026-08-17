/**
 * Desk Gateway Web UI 结构回归测试。
 *
 * 该测试只验证静态页面契约：右侧不再重复展示高度、番茄时钟默认折叠，
 * 且交互代码不再调用浏览器原生 alert/confirm/prompt。
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const wwwDir = path.join(__dirname, '..', 'www');
const html = fs.readFileSync(path.join(wwwDir, 'index.html'), 'utf8');
const app = fs.readFileSync(path.join(wwwDir, 'app.js'), 'utf8');

/** 确保关键 DOM 契约存在，避免后续改版绕过本次交互约束。 */
function assertContains(source, fragment, message) {
  assert.ok(source.includes(fragment), message);
}

assert.doesNotMatch(
  app,
  /window\.(?:alert|confirm|prompt)\s*\(/,
  'Web 交互不得恢复浏览器原生弹窗',
);
assert.doesNotMatch(
  html,
  /id="height"/,
  '右侧控制区不得重复展示实时高度',
);

assertContains(html, 'id="reminderQuickStart"', '番茄时钟需要快速开始按钮');
assertContains(html, 'id="reminderExpandButton"', '番茄时钟需要展开按钮');
assertContains(
  html,
  'id="reminderDetails" class="reminder-details" hidden',
  '番茄时钟配置区必须默认折叠',
);
assertContains(html, 'id="appDialog"', '页面需要统一的自定义弹窗');
assertContains(app, 'function openAppDialog(options)', '交互需要复用统一弹窗入口');
assertContains(app, 'function confirmAction(options)', '危险操作需要复用确认弹窗');
assertContains(
  app,
  'DeskReminderControl.previewActionHint',
  '试听提示必须按设备 playing 快照清除，不能只写死文案',
);

console.log('web-ui-structure.test: ok');
