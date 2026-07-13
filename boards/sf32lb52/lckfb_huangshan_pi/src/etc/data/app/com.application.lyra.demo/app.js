
        export default function(global, globalThis, window, $app_exports$, $app_evaluate$){
          var org_app_require = $app_require$;
        
          (function(global, globalThis, window, $app_exports$, $app_evaluate$){
            var setTimeout = global.setTimeout;
            var setInterval = global.setInterval;
            var clearTimeout = global.clearTimeout;
            var clearInterval = global.clearInterval;
            var $app_require$ = global.$app_require$ || org_app_require

            // 转换动态 style 的函数
            var $translateStyle$ = function (value) {
              if (typeof value === 'string') {
                return Object.fromEntries(value.split(';').filter(item => Boolean(item && item.trim())).map(
                  item => {
                    const matchs = item.match(/([^:]+):(.*)/)
                    if (matchs && matchs.length> 2) {
                      return [matchs[1].trim().replace(/-([a-z])/g, (_, match) => match.toUpperCase()), matchs[2].trim()]
                    }
                    return []
                  }))}
              return value
            }
        
            var createPageHandler = function() {
              return (() => { // webpackBootstrap
var __webpack_modules__ = ({
"./src/js/pubsub.js": 
/*!**************************!*\
  !*** ./src/js/pubsub.js ***!
  \**************************/
(function (__unused_webpack_module, exports) {
"use strict";


Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.createOrRetrieveInst = createOrRetrieveInst;
exports["default"] = void 0;
/**
 * @file pubsub.js 提供发布订阅的能力
 */

/**
 * 提供Publish-Subscribe模型
 */
class Pubsub {
  constructor(name) {
    this.name = name;
    this.eventMap = {};
  }

  /**
   * 订阅事件
   * @param type {string} 事件名称
   * @param fn {function} 响应函数
   * @param options {object} 暂时保留
   * @return {*}
   */
  subscribe(type, fn, options) {
    if (options && options.once) {
      const fnOnce = args => {
        fn(args);
        this.remove(type, fnOnce);
      };
      return this.subscribe(type, fnOnce);
    }
    this.eventMap[type] = this.eventMap[type] || [];
    if (typeof fn === 'function') {
      const list = this.eventMap[type];
      if (list.indexOf(fn) === -1) {
        list.push(fn);
      }
    }
  }

  /**
   * 发布事件
   * @param type {string} 事件名称
   * @param args {array} 事件触发时的参数
   * @return {*}
   */
  publish(type, args) {
    let lastRet = null;
    const list = this.eventMap[type] || [];
    for (let i = 0, len = list.length; i < len; i++) {
      lastRet = list[i](args, lastRet);
    }
    return lastRet;
  }

  /**
   * 删除事件订阅
   * @param type {string} 事件名称
   * @param fn {function} 响应函数
   */
  remove(type, fn) {
    if (!this.eventMap[type]) return;
    const list = this.eventMap[type];
    const index = list.indexOf(fn);
    if (index > -1) {
      list.splice(index, 1);
    }
  }
}

// 实例缓存
exports["default"] = Pubsub;
const modelCache = {};

/**
 * 用于创建或获取一个指定名称的Pubsub模型的实例
 * @param name {string} 通过名称创建不同的实例
 * @return {*}
 */
function createOrRetrieveInst(name) {
  if (!modelCache[name]) {
    modelCache[name] = new Pubsub(name);
  }
  return modelCache[name];
}

}),
"./src/manifest.json": 
/*!***************************!*\
  !*** ./src/manifest.json ***!
  \***************************/
(function (module) {
"use strict";
module.exports = JSON.parse('{"package":"com.application.lyra.demo","name":"天琴demo","versionName":"1.0.0","versionCode":1,"minPlatformVersion":1000,"icon":"/common/logo.png","deviceTypeList":["watch"],"features":[{"name":"system.internal.messagecenter"},{"name":"system.velaclaw"},{"name":"system.record"},{"name":"system.storage"}],"permissions":[{"name":"hapjs.permission.APPFUNCTION"},{"name":"hapjs.permission.RECORD"}],"appFunctions":{"functions":[{"id":"createNote","description":"创建一条新备忘录","operationType":"create","handler":"appFunctions.createNote","preconditions":{"requiresAuth":true,"requiresCapabilities":["storage"]},"confirmation":{"required":false},"parameters":{"title":{"type":"string","description":"备忘录标题","required":true,"maxLength":100},"content":{"type":"string","description":"备忘录内容","required":true,"maxLength":10000}},"returns":{"type":"object","properties":{"id":{"type":"number","description":"备忘录ID"},"title":{"type":"string"},"content":{"type":"string"},"createdAt":{"type":"string"}}}},{"id":"listNotes","description":"列出所有备忘录","operationType":"query","handler":"appFunctions.listNotes","preconditions":{"requiresAuth":true},"parameters":{},"returns":{"type":"array","items":{"type":"object","properties":{"id":{"type":"number"},"title":{"type":"string"},"content":{"type":"string"}}}}},{"id":"deleteNote","description":"删除一条备忘录，不可撤销","operationType":"delete","handler":"appFunctions.deleteNote","preconditions":{"requiresAuth":true},"confirmation":{"required":true,"promptTemplate":"确认删除备忘录「${title}」吗？此操作不可撤销。"},"constraints":{"rateLimitPerMinute":10},"parameters":{"noteId":{"type":"number","description":"要删除的备忘录ID","required":true},"title":{"type":"string","description":"备忘录标题（用于确认提示）"}},"returns":{"type":"object","properties":{"deleted":{"type":"boolean"}}}}]},"config":{"logLevel":"log","designWidth":466},"router":{"entry":"pages/velaclaw","pages":{"pages/index":{"component":"index"},"pages/longlist":{"component":"index"},"pages/velaclaw":{"component":"index"},"pages/connectivity":{"component":"index"},"pages/appfunction":{"component":"index"}}}}')

}),

});
/************************************************************************/
// The module cache
var __webpack_module_cache__ = {};

// The require function
function __webpack_require__(moduleId) {

// Check if module is in cache
var cachedModule = __webpack_module_cache__[moduleId];
if (cachedModule !== undefined) {
return cachedModule.exports;
}
// Create a new module (and put it into the cache)
var module = (__webpack_module_cache__[moduleId] = {
exports: {}
});
// Execute the module function
__webpack_modules__[moduleId](module, module.exports, __webpack_require__);

// Return the exports of the module
return module.exports;

}

/************************************************************************/
// webpack/runtime/rspack_version
(() => {
__webpack_require__.rv = () => ("1.5.6")
})();
// webpack/runtime/rspack_unique_id
(() => {
__webpack_require__.ruid = "bundler=rspack@1.5.6";

})();
/************************************************************************/
var __webpack_exports__ = {};
// This entry needs to be wrapped in an IIFE because it needs to be isolated against other modules in the chunk.
(() => {

/*!*******************************!*\
  !*** ./src/app.ux?uxType=app ***!
  \*******************************/
var $app_style$ = []
var $app_script$ = function __scriptModule__(module, exports, $app_require$) {	"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.default = void 0;
var _pubsub = __webpack_require__(/*! ./js/pubsub.js */ "./src/js/pubsub.js");
const noteStore = {
  notes: [],
  nextId: 1
};
const pubsubModel = (0, _pubsub.createOrRetrieveInst)('appPubSub');
function publishAppFunctionMsg(evtName, data) {
  console.log('[App] publishMessage: ' + evtName + ', data=' + JSON.stringify(data));
  pubsubModel.publish(evtName, data);
}
const appFunctions = {
  /**
   * 系统在执行 requiresAuth 函数前调用此钩子检查登录态
   */
  $checkAuth() {
    // 演示用：始终返回已登录
    // 实际场景中应检查 storage 中的 token
    console.log('[AppFunction app.ux] $checkAuth called');
    return {
      authenticated: true,
      userId: 'demo_user'
    };
  },
  /**
   * 创建备忘录
   */
  createNote(context, params) {
    console.log('[AppFunction app.ux] createNote called by ' + context.callerPackage, context.callerType);
    const note = {
      id: noteStore.nextId++,
      title: params.title,
      content: params.content,
      createdAt: new Date().toISOString()
    };
    noteStore.notes.push(note);
    console.log('[AppFunction app.ux] createNote success, id=' + note.id + ', total=' + noteStore.notes.length + ', notes=' + JSON.stringify(noteStore.notes));

    // 创建成功后发布事件，通知前端更新备忘录列表
    if (context.callerType === 'agent') {
      console.log('[AppFunction app.ux] ---------------- calling');
      // 来自 Agent 的调用，发布事件通知前端更新
      // const app = this.$app
      // console.log('[AppFunction app.ux] ---------------- app', app)

      publishAppFunctionMsg('noteCreated', {
        noteId: note.id,
        title: note.title
      });
    }
    return note;
  },
  /**
   * 列出所有备忘录
   */
  listNotes(context, params) {
    console.log('[AppFunction app.ux] listNotes called, count=' + noteStore.notes.length);
    return noteStore.notes.length > 0 ? noteStore.notes.slice() : null;
  },
  /**
   * 删除备忘录
   */
  deleteNote(context, params) {
    console.log('[AppFunction app.ux] deleteNote called, noteId=' + params.noteId);
    const idx = noteStore.notes.findIndex(function (n) {
      return n.id === params.noteId;
    });
    if (idx === -1) {
      throw {
        code: -32001,
        message: 'Note not found: ' + params.noteId
      };
    }
    noteStore.notes.splice(idx, 1);
    console.log('[AppFunction app.ux] deleteNote success, remaining=' + noteStore.notes.length);
    return {
      deleted: true
    };
  }
};
var _default = exports.default = {
  noteStore,
  pubsubModel,
  $appFunctions: appFunctions,
  onAppFunctionInvoked(functionId, callerInfo) {
    console.log('[AppFunction app.ux] onAppFunctionInvoked: ' + functionId + ' by ' + callerInfo.callerPackage);
  },
  onCreate() {
    console.log('[App] created with AppFunctions: ' + Object.keys(appFunctions).filter(function (k) {
      return k[0] !== '$';
    }).join(', '));
  },
  onDestroy() {
    console.log('[App] destroyed');
  }
};

}
$app_script$({}, $app_exports$, $app_require$);
$app_exports$.default.style = $app_style$;
$app_exports$.default.manifest = __webpack_require__(/*! ./manifest.json */ "./src/manifest.json")
})();

})()
;
            }
        
            return createPageHandler();
          })(global, globalThis, window, $app_exports$, $app_evaluate$)
        }