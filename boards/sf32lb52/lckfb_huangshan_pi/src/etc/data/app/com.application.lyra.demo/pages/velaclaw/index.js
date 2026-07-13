
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

/*!*************************************************!*\
  !*** ./src/pages/velaclaw/index.ux?uxType=page ***!
  \*************************************************/
var $app_style$ = [[[[0,"page"]],{"paddingTop":"60px","paddingRight":"60px","paddingBottom":"60px","paddingLeft":"60px","flexDirection":"column"}],[[[0,"column"]],{"flexDirection":"column"}],[[[0,"row"]],{"flexDirection":"row"}],[[[0,"header"]],{"fontWeight":"bold","marginBottom":"10px","marginTop":"20px","fontSize":"30px"}],[[[0,"reply"]],{"marginBottom":"10px","marginTop":"20px","fontSize":"30px"}],[[[0,"gray"]],{"color":"gray"}],[[[0,"box"]],{"width":"200px","height":"80px","marginBottom":"10px","backgroundColor":"purple"}],[[[0,"group"]],{"marginBottom":"15px","alignItems":"center"}],[[[0,"label"]],{"marginTop":"10px"}],[[[0,"border"]],{"borderTopColor":"purple","borderRightColor":"purple","borderBottomColor":"purple","borderLeftColor":"purple","borderStyle":"solid","borderTopWidth":"1px","borderRightWidth":"1px","borderBottomWidth":"1px","borderLeftWidth":"1px"}],[[[0,"tips"]],{"paddingTop":"5px","paddingRight":"5px","paddingBottom":"5px","paddingLeft":"5px","backgroundColor":"rgb(240,228,204)","color":"rgb(230,151,4)","borderRadius":"5px","marginTop":"5px","marginBottom":"5px"}],[[[0,"code"]],{"backgroundColor":"#f0f0f0","borderRadius":"5px","borderTopColor":"lightgray","borderRightColor":"lightgray","borderBottomColor":"lightgray","borderLeftColor":"lightgray","borderStyle":"solid","borderTopWidth":"1px","borderRightWidth":"1px","borderBottomWidth":"1px","borderLeftWidth":"1px","paddingTop":"2px","paddingRight":"4px","paddingBottom":"2px","paddingLeft":"4px","fontSize":"24px","color":"#000000"}],[[[0,"btn"]],{"height":"50px","width":"380px","textAlign":"center","borderRadius":"5px","marginBottom":"20px","color":"#ffffff","fontSize":"26px","backgroundColor":"#0faeff"}],[[[0,"gray-btn"]],{"width":"240px","height":"50px","textAlign":"center","borderRadius":"5px","marginBottom":"20px","color":"#ffffff","fontSize":"26px","backgroundColor":"#999"}],[[[2,"text"]],{"fontSize":"26px"}]]
var $app_script$ = function __scriptModule__(module, exports, $app_require$) {	"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.default = void 0;
var _system = _interopRequireDefault($app_require$("@app-module/system.velaclaw"));
function _interopRequireDefault(e) { return e && e.__esModule ? e : { default: e }; }
var _default = exports.default = {
  private: {
    sendData: '',
    receiveData: '',
    reply: '',
    cur: 0,
    queryList: ['北京今天天气怎么样', '从上海去北京怎么走最快', '明天早上7点叫我起床', '今晚吃什么？', '上地到清河路况堵吗']
  },
  handleNext() {
    this.cur = (this.cur + 1) % this.queryList.length;
    console.log('------------- handleNext, cur:', this.cur);
    this.reply = "";
  },
  handleAsk() {
    console.log('------------- 发起对话 ----------------');
    // 回调方式
    _system.default.ask({
      query: this.queryList[this.cur],
      success: res => {
        console.log('------------- handleAsk success, AI reply:', res);
        this.reply = res.reply;
        console.log('------------- handleAsk success, AI reply:', this.reply);
      },
      fail: function (data, code) {
        console.log('------------- handleAsk fail, code:', code);
      },
      complete: function () {
        console.log('------------- handleAsk complete');
      }
    });
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
"__opts__":{"classList":["column","page"]}}, [aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["column","group"]}}, [aiot.__ce__("input", {"__vm__":_vm_,
"__opts__":{"type":"button",
"classList":["btn"],
"events":{"click":function(evt) { return _vm_.handleAsk(evt) }},
"value":function() { return "ask:" + (_vm_.queryList[_vm_.cur]) }}}, []),
aiot.__ce__("input", {"__vm__":_vm_,
"__opts__":{"type":"button",
"classList":["gray-btn"],
"events":{"click":function(evt) { return _vm_.handleNext(evt) }},
"value":"换一换"}}, []),
aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["reply"],
"value":function() { return _vm_.reply }}}, [])])])

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