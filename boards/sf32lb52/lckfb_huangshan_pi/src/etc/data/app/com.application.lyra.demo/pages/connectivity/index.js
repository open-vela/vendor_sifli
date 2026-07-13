
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
"./src/js/messagecenter.js": 
/*!*********************************!*\
  !*** ./src/js/messagecenter.js ***!
  \*********************************/
(function (__unused_webpack_module, exports) {
"use strict";


Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports["default"] = void 0;
var _systemInternal = _interopRequireDefault($app_require$("@app-module/system.internal.messagecenter"));
function _interopRequireDefault(e) {
  return e && e.__esModule ? e : {
    default: e
  };
}
function ab2str(buf) {
  console.log('ab2str fun called decodeuri');
  // return decodeURIComponent(String.fromCharCode.apply(null, new Uint8Array(buf)))

  // fix: 转换中文乱码
  const uint8Array = new Uint8Array(buf);
  let str = '';
  for (let i = 0; i < uint8Array.length; i++) {
    str += String.fromCharCode(uint8Array[i]);
  }
  return decodeURIComponent(escape(str));
}

/**
 * 一对多消息分发
 */
var _default = exports["default"] = {
  // 订阅消息
  mcSubscribe(topic, cb) {
    _systemInternal.default.subscribe({
      topic: topic,
      listener: function (deviceid, topic, data) {
        let {
          basedata,
          extenddata
        } = data;
        console.log('mcSubscribe listener called basedata: ', basedata, extenddata);
        if (basedata) {
          basedata = ab2str(basedata);
        }
        if (extenddata) {
          extenddata = ab2str(extenddata);
        }
        console.log(`topic ${topic}, base data: ${basedata}, extra data: ${extenddata}, type:${data.type} from device ${deviceid}`);
        cb && cb(extenddata);
      },
      success: function () {
        console.log(`messagecenter.subscribe  ${topic} success`);
      },
      fail: function (data, code) {
        console.log(`messagecenter.subscribe handling fail, code = ${code}`);
      },
      complete: function () {
        console.log('messagecenter.subscribe complete');
      }
    });
  },
  // 取消订阅消息
  mcUnsubscribe(topic) {
    _systemInternal.default.unsubscribe({
      topic: topic,
      success: function () {
        console.log(`messagecenter.unsubscribe  ${topic} success`);
      },
      fail: function (data, code) {
        console.log(`messagecenter.unsubscribe handling fail, code = ${code}`);
      },
      complete: function () {
        console.log('messagecenter.unsubscribe complete');
      }
    });
  },
  // 发布消息
  mcPublish(topic, pubData) {
    _systemInternal.default.publish({
      topic: topic,
      data: pubData,
      success: function () {
        console.log('messagecenter.publish success');
      },
      fail: function (data, code) {
        console.log(`messagecenter.publish handling fail, code = ${code}`);
      },
      complete: function () {
        console.log('messagecenter.publish complete');
      }
    });
  },
  // 取消发布消息
  mcUnpublish(topic) {
    _systemInternal.default.unpublish({
      topic: topic,
      success: function () {
        console.log('messagecenter.unpublish success');
      },
      fail: function (data, code) {
        console.log(`messagecenter.unpublish handling fail, code = ${code}`);
      },
      complete: function () {
        console.log('messagecenter.unpublish complete');
      }
    });
  }
};

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

/*!*****************************************************!*\
  !*** ./src/pages/connectivity/index.ux?uxType=page ***!
  \*****************************************************/
var $app_style$ = [[[[0,"full"]],{"width":"100%","height":"100%"}],[[[0,"bg-black"]],{"backgroundColor":"#000000"}],[[[0,"column"]],{"flexDirection":"column"}],[[[0,"text-center"]],{"justifyContent":"center"}],[[[0,"center"]],{"justifyContent":"center","alignItems":"center"}],[[[0,"items-stretch"]],{"alignItems":"stretch"}],[[[0,"flex-1"]],{"flex":1}],[[[0,"page"]],{"justifyContent":"center","alignItems":"center","paddingTop":"10px","paddingRight":"10px","paddingBottom":"10px","paddingLeft":"10px","backgroundColor":"#000000"}],[[[0,"block"]],{"width":"100%","marginBottom":"5px"}],[[[0,"btn"]],{"width":"120px","height":"50px","textAlign":"center","color":"#ffffff","fontSize":"24px","marginTop":"10px","marginRight":"25px","marginBottom":"10px","marginLeft":"25px","paddingTop":"10px","paddingRight":"10px","paddingBottom":"10px","paddingLeft":"10px","borderRadius":"10px","backgroundColor":"#4b83e6"}]]
var $app_script$ = function __scriptModule__(module, exports, $app_require$) {	"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.default = void 0;
var _system = _interopRequireDefault($app_require$("@app-module/system.router"));
var _messagecenter = _interopRequireDefault(__webpack_require__(/*! ../../js/messagecenter.js */ "./src/js/messagecenter.js"));
function _interopRequireDefault(e) { return e && e.__esModule ? e : { default: e }; }
// import NW from './networking.js';
var _default = exports.default = {
  private: {
    topic: 'connectivity_test',
    subResponse: null
  },
  onClickClearBtn() {
    this.subResponse = null;
  },
  onClickSubBtn() {
    console.log('onClickSubBtn topic:', this.topic);
    _messagecenter.default.mcSubscribe(this.topic, data => {
      console.log('mcSubscribe callback data:', data);
      this.subResponse = data;
    });
  },
  onClickUnsubBtn() {
    console.log('onClickUnsubBtn topic:', this.topic);
    _messagecenter.default.mcUnsubscribe(this.topic);
  },
  onClickPubBtn() {
    let uid = new Date().getTime().toString();
    // const u8Array = new Uint8Array([0x48, 0x65, 0x6C, 0x6C, 0x6F]); // "Hello" 的 UTF-8 编码
    const u8Array = new Uint8Array(Array.from({
      length: 1024 * 100
    }, (_, i) => [0x48, 0x65, 0x6C, 0x6C, 0x6F][i % 5])); // 100KB 的 "Hello" 数据
    // const u8Array = new Uint8Array(Array.from({ length: 2040 }, (_, i) => [0x48, 0x65, 0x6C, 0x6C, 0x6F][i % 5]));

    // let extData = {
    //   type: 'createPageData',
    //   id: uid,
    // }

    let pubData = {
      basedata: null,
      // extenddata: JSON.stringify(extData),
      extenddata: u8Array,
      type: 1
    };
    console.log(`onClickPubBtn topic: ${this.topic}, pubData:`, pubData, Date.now());
    _messagecenter.default.mcPublish(this.topic, pubData);
  },
  onClickUnpubBtn() {
    console.log('onClickUnpubBtn topic:', this.topic);
    _messagecenter.default.mcUnpublish(this.topic);
  },
  onInit() {
    console.log('index onInit');
  },
  onShow() {
    console.log('index onShow');
  },
  onHide() {
    console.log('index onHide');
  },
  onDestroy() {
    console.log('index onDestroy');
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
"__opts__":{"classList":["page","full","bg-black"]}}, [aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["column"]}}, [aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["block","center"]}}, [aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"value":function() { return "topicName: " + (_vm_.topic) }}}, [])]),
aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["block","center"]}}, [aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["btn"],
"events":{"click":function(evt) { return _vm_.onClickSubBtn(evt) }},
"value":"订阅"}}, []),
aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["btn"],
"events":{"click":function(evt) { return _vm_.onClickUnsubBtn(evt) }},
"value":"取消订阅"}}, [])]),
aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["block","center"]}}, [aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["btn"],
"events":{"click":function(evt) { return _vm_.onClickPubBtn(evt) }},
"value":"发布"}}, []),
aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["btn"],
"events":{"click":function(evt) { return _vm_.onClickUnpubBtn(evt) }},
"value":"取消发布"}}, [])]),
aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["block","center"]}}, [aiot.__ci__({"__vm__":_vm_,
"__opts__":{"shown":function(){ return (_vm_.subResponse); }}}, function(){
          return [aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"value":function() { return "basedata:" + (_vm_.subResponse) }}}, [])]
        })])])])

    }
$app_exports$['entry'] = function ($app_exports$) {
$app_script$({}, $app_exports$, $app_require$);
$app_exports$.default.template = $app_template$;
$app_exports$.default.style = $app_style$;
}
})();

})()
;
            }
        
            return createPageHandler();
          })(global, globalThis, window, $app_exports$, $app_evaluate$)
        }