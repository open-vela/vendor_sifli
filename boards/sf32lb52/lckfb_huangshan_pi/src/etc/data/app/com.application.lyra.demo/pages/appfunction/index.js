
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
var __webpack_modules__ = ({});
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

/*!****************************************************!*\
  !*** ./src/pages/appfunction/index.ux?uxType=page ***!
  \****************************************************/
var $app_style$ = [[[[0,"page"]],{"paddingTop":"30px","paddingRight":"30px","paddingBottom":"30px","paddingLeft":"30px","flexDirection":"column"}],[[[0,"column"]],{"flexDirection":"column"}],[[[0,"header"]],{"fontWeight":"bold","fontSize":"32px","marginBottom":"15px","color":"#ffffff"}],[[[0,"label"]],{"fontSize":"24px","color":"#aaaaaa","marginBottom":"5px","marginTop":"10px"}],[[[0,"code"]],{"fontSize":"22px","color":"#66ccff","backgroundColor":"#222222","paddingTop":"4px","paddingRight":"8px","paddingBottom":"4px","paddingLeft":"8px","borderRadius":"4px","marginBottom":"4px"}],[[[0,"result"]],{"width":"320px","fontSize":"24px","color":"#00ff88","backgroundColor":"#1a1a2e","paddingTop":"10px","paddingRight":"10px","paddingBottom":"10px","paddingLeft":"10px","borderRadius":"6px","marginTop":"5px"}],[[[0,"log"]],{"fontSize":"20px","color":"#888888","marginBottom":"2px"}],[[[0,"group"]],{"marginBottom":"10px","justifyItems":"center","alignItems":"center"}],[[[0,"box"]],{"width":"100%","height":"240px","paddingTop":"5px","paddingRight":"5px","paddingBottom":"5px","paddingLeft":"5px","borderTopColor":"#333333","borderRightColor":"#333333","borderBottomColor":"#333333","borderLeftColor":"#333333","borderStyle":"solid","borderTopWidth":"1px","borderRightWidth":"1px","borderBottomWidth":"1px","borderLeftWidth":"1px","borderRadius":"6px"}],[[[0,"btn"]],{"height":"50px","width":"320px","textAlign":"center","borderRadius":"5px","marginBottom":"10px","color":"#ffffff","fontSize":"24px","backgroundColor":"#0faeff"}],[[[0,"btn-danger"]],{"height":"50px","width":"380px","textAlign":"center","borderRadius":"5px","marginBottom":"10px","color":"#ffffff","fontSize":"24px","backgroundColor":"#ff4444"}],[[[0,"gray-btn"]],{"height":"50px","width":"380px","textAlign":"center","borderRadius":"5px","marginBottom":"10px","color":"#ffffff","fontSize":"24px","backgroundColor":"#666666"}]]
var $app_script$ = function __scriptModule__(module, exports, $app_require$) {	"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.default = void 0;
var _default = exports.default = {
  private: {
    notes: [],
    functions: [{
      id: 'createNote',
      operationType: 'create'
    }, {
      id: 'listNotes',
      operationType: 'query'
    }, {
      id: 'deleteNote',
      operationType: 'delete'
    }]
  },
  refreshNotes() {
    this.notes = this.$app.$def.noteStore.notes.slice();
    console.log('[AppFunction page] refreshNotes, total=' + this.notes.length + ', notes=' + JSON.stringify(this.notes));
  },
  onInit() {
    console.log('[AppFunction page] onInit called');
    this.pubsubModel = this.$app.$def.pubsubModel;
    if (!this.pubsubModel) {
      console.warn('[AppFunction page] pubsubModel not found on app def');
      return;
    }
    this.pubsubModel.subscribe('noteCreated', data => {
      console.log('[AppFunction page] received noteCreated event: ' + JSON.stringify(data));
      console.log('收到 noteCreated 事件: ' + JSON.stringify(data));
      this.refreshNotes();
    });
  },
  testCreateNote() {
    console.log('调用 createNote...');
    try {
      const app = this.$app.$def;
      const handler = app.$appFunctions.createNote;
      if (!handler) {
        return;
      }
      const context = {
        callerPackage: 'com.test.agent',
        callerType: 'agent',
        timestamp: Date.now()
      };
      const params = {
        title: '测试备忘录 ' + Date.now(),
        content: '通过 AppFunction 创建的测试备忘录'
      };
      const note = handler(context, params);
      console.log('createNote 成功: id=' + note.id);
    } catch (e) {
      console.log('createNote 失败: ' + e.message);
    }
  },
  testListNotes() {
    console.log('调用 listNotes...');
    try {
      const app = this.$app.$def;
      const handler = app.$appFunctions.listNotes;
      if (!handler) {
        return;
      }
      const context = {
        callerPackage: 'com.test.agent',
        callerType: 'agent',
        timestamp: Date.now()
      };
      const notes = handler(context, {});
      if (notes && notes.length > 0) {
        console.log('listNotes 成功: ' + notes.length + ' 条');
      } else {
        console.log('listNotes 成功: 0 条');
      }
    } catch (e) {
      console.log('listNotes 失败: ' + e.message);
    }
  },
  testDeleteNote() {
    console.log('调用 deleteNote...');
    try {
      const app = this.$app.$def;
      const listHandler = app.$appFunctions.listNotes;
      const notes = listHandler ? listHandler({
        callerPackage: 'test',
        callerType: 'agent',
        timestamp: Date.now()
      }, {}) : null;
      if (!notes || notes.length === 0) {
        console.log('deleteNote 跳过: 无数据');
        return;
      }
      const lastNote = notes[notes.length - 1];
      const handler = app.$appFunctions.deleteNote;
      if (!handler) {
        return;
      }
      const context = {
        callerPackage: 'com.test.agent',
        callerType: 'agent',
        timestamp: Date.now()
      };
      const res = handler(context, {
        noteId: lastNote.id,
        title: lastNote.title
      });
      console.log('deleteNote 成功: id=' + lastNote.id);
    } catch (e) {
      console.log('deleteNote 失败: ' + (e.message || e.code));
    }
  },
  testCheckAuth() {
    console.log('调用 $checkAuth...');
    try {
      const app = this.$app.$def;
      const checkAuth = app.$appFunctions.$checkAuth;
      if (!checkAuth) {
        console.log('$checkAuth 未实现');
        return;
      }
      const authResult = checkAuth();
      console.log('$checkAuth 结果: authenticated=' + authResult.authenticated);
    } catch (e) {
      console.log('$checkAuth 失败: ' + e.message);
    }
  }
};


  const moduleOwn = exports.default || module.exports
  const accessors = ['public', 'protected', 'private']

  if (moduleOwn.data && accessors.some(function (acc) { return moduleOwn[acc] })) {
    throw new Error('页面VM对象中的属性data不可与"' + accessors.join(',') + '"同时存在，请使用private替换data名称')
  }
  else if (!moduleOwn.data) {
    moduleOwn.data = {}
    moduleOwn._descriptor = {}
    accessors.forEach(function (acc) {
      const accType = typeof moduleOwn[acc]
      if (accType === 'object') {
        moduleOwn.data = Object.assign(moduleOwn.data, moduleOwn[acc])
        for (const name in moduleOwn[acc]) {
          moduleOwn._descriptor[name] = { access: acc }
        }
      }
      else if (accType === 'function') {
        console.warn('页面VM对象中的属性' + acc + '的值不能是函数，请使用对象')
      }
    })
  }

}
var $app_template$ = function (vm) {
      const _vm_ = vm || this
      return aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["column","page"]}}, [aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["header"],
"value":"AppFunction 测试"}}, []),
aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["column","group","box"]}}, [aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["label"],
"value":"备忘录："}}, []),
aiot.__cf__({"__vm__":_vm_,
"__opts__":{"exp":function() { return _vm_.notes },
"key":"$idx",
"value":"$item"}}, function($idx, $item, ){
          return [aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["code"],
"value":function() { return ($item.title) + " (" + ($item.content) + ")" }}}, [])]
        })]),
aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["column","group"]}}, [aiot.__ce__("input", {"__vm__":_vm_,
"__opts__":{"type":"button",
"classList":["btn"],
"events":{"click":function(evt) { return _vm_.refreshNotes(evt) }},
"value":"刷新Notes"}}, [])])])

    }
$app_exports$['entry'] = function ($app_exports$) {
$app_script$({}, $app_exports$, $app_require$);
$app_exports$.default.template = $app_template$;
$app_exports$.default.style = $app_style$;
}
})()
;
            }
        
            return createPageHandler();
          })(global, globalThis, window, $app_exports$, $app_evaluate$)
        }