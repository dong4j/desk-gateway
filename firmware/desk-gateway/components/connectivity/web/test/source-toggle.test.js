/**
 * @file source-toggle.test.js
 * @brief 回归验证入口开关提示不受并发状态轮询改写。
 *
 * app.js 属于直接运行的页面脚本，没有导出内部绑定函数。测试只提取真实的
 * bindSourceToggle 定义，并模拟 API 等待期间状态轮询反向更新 checkbox。
 */
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const appPath = path.join(__dirname, '..', 'www', 'app.js');
const appSource = fs.readFileSync(appPath, 'utf8');
const functionStart = appSource.indexOf('  function bindSourceToggle');
const functionEnd = appSource.indexOf('\n  bindSourceToggle(', functionStart);
assert.notEqual(functionStart, -1, 'bindSourceToggle definition must exist');
assert.notEqual(functionEnd, -1, 'bindSourceToggle call site must exist');
const functionSource = appSource.slice(functionStart, functionEnd);

/** 构造页面中的真实绑定函数，同时替换其三个外部依赖。 */
function loadBindSourceToggle(api, document, tick) {
  return Function(
    'api', 'document', 'tick',
    `'use strict';\n${functionSource}\nreturn bindSourceToggle;`,
  )(api, document, tick);
}

/** 请求期间即使 checkbox 被轮询改写，提示仍必须表达用户刚才的选择。 */
async function verifyPanelMessage(desired, polled, expectedMessage) {
  const input = {
    checked: desired,
    disabled: false,
    nextElementSibling: { textContent: '允许原厂面板操作' },
  };
  const message = { textContent: '' };
  let requestBody = null;
  let refreshCount = 0;
  const bindSourceToggle = loadBindSourceToggle(
    async (requestPath, method, body) => {
      assert.equal(requestPath, '/api/v1/desk/access');
      assert.equal(method, 'POST');
      requestBody = body;
      input.checked = polled;
    },
    { getElementById: () => message },
    async () => { refreshCount += 1; },
  );

  bindSourceToggle(input, 'panel');
  await input.onchange();

  assert.deepEqual(requestBody, { source: 'panel', enabled: desired });
  assert.equal(message.textContent, expectedMessage);
  assert.equal(refreshCount, 1);
  assert.equal(input.disabled, false);
}

async function main() {
  await verifyPanelMessage(true, false, '原厂面板操作已开启');
  await verifyPanelMessage(false, true, '原厂面板已锁定，仅显示高度');
  console.log('web source toggle race vectors: OK');
}

void main();
