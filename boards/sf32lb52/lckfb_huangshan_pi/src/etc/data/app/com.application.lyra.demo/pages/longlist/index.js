
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
  !*** ./src/pages/longlist/index.ux?uxType=page ***!
  \*************************************************/
var $app_style$ = [[[[0,"full"]],{"width":"100%","height":"100%"}],[[[0,"bg-black"]],{"backgroundColor":"#000000"}],[[[0,"column"]],{"flexDirection":"column"}],[[[0,"text-center"]],{"justifyContent":"center"}],[[[0,"center"]],{"justifyContent":"center","alignItems":"center"}],[[[0,"items-stretch"]],{"alignItems":"stretch"}],[[[0,"flex-1"]],{"flex":1}],[[[0,"page"]],{"backgroundColor":"#111518","flexDirection":"column"}],[[[0,"header"]],{"width":"100%","paddingTop":"6px","paddingRight":"12px","paddingBottom":"6px","paddingLeft":"12px","backgroundColor":"#1a1d21","borderBottomWidth":"1px","borderBottomStyle":"solid","borderBottomColor":"#2a2d31","justifyContent":"center","alignItems":"center"}],[[[0,"stat-text"]],{"fontSize":"12px","color":"#aaaaaa"}],[[[0,"add-section"]],{"width":"100%","paddingTop":"6px","paddingRight":"12px","paddingBottom":"6px","paddingLeft":"12px","backgroundColor":"#1a1d21","borderBottomWidth":"1px","borderBottomStyle":"solid","borderBottomColor":"#2a2d31","flexDirection":"row"}],[[[0,"btn"]],{"height":"28px","textAlign":"center","lineHeight":"28px","borderRadius":"4px","fontSize":"12px","fontWeight":"bold","color":"#ffffff"}],[[[0,"btn-primary"]],{"backgroundColor":"#4078ff"}],[[[0,"btn-danger"]],{"backgroundColor":"#ff4444"}],[[[0,"btn-success"]],{"backgroundColor":"#44aa44"}],[[[0,"btn-secondary"]],{"backgroundColor":"#666666"}],[[[0,"list-container"]],{"width":"100%","flex":1,"flexDirection":"column","alignItems":"center","borderTopColor":"#aa9d31","borderRightColor":"#aa9d31","borderBottomColor":"#aa9d31","borderLeftColor":"#aa9d31","borderStyle":"solid","borderTopWidth":"2px","borderRightWidth":"2px","borderBottomWidth":"2px","borderLeftWidth":"2px","backgroundColor":"#111518"}],[[[0,"task-list"]],{"borderTopColor":"#9a2d31","borderRightColor":"#9a2d31","borderBottomColor":"#9a2d31","borderLeftColor":"#9a2d31","borderStyle":"solid","borderTopWidth":"1px","borderRightWidth":"1px","borderBottomWidth":"1px","borderLeftWidth":"1px","width":"90%","height":"100%"}],[[[0,"list-item-wrapper"]],{"width":"100%","height":"46px","backgroundColor":"#666666"}],[[[0,"task-item"]],{"width":"100%","paddingTop":"8px","paddingRight":"12px","paddingBottom":"8px","paddingLeft":"12px","flexDirection":"row","alignItems":"center","borderBottomWidth":"1px","borderBottomStyle":"solid","borderBottomColor":"#2a2d31"}],[[[0,"checkbox"]],{"width":"20px","height":"16px"}],[[[0,"content-area"]],{"flex":1,"flexDirection":"row","alignItems":"center"}],[[[0,"task-text"]],{"fontSize":"20px","color":"#ffffff"}],[[[0,"completed"]],{"textDecoration":"line-through","color":"#888888"}],[[[0,"priority-dot"]],{"width":"6px","height":"6px","borderRadius":"3px"}],[[[0,"action-btn"]],{"width":"45px","height":"26px","textAlign":"center","lineHeight":"26px","borderRadius":"3px","fontSize":"11px","color":"#ffffff"}],[[[0,"edit-btn"]],{"backgroundColor":"#4078ff"}],[[[0,"delete-btn"]],{"backgroundColor":"#ff4444"}],[[[0,"edit-mode"]],{"width":"85%","paddingTop":"8px","paddingRight":"12px","paddingBottom":"8px","paddingLeft":"12px","backgroundColor":"#1a1d21","borderBottomWidth":"1px","borderBottomStyle":"solid","borderBottomColor":"#2a2d31"}],[[[0,"edit-input"]],{"width":"100%","height":"28px","paddingTop":"4px","paddingRight":"8px","paddingBottom":"4px","paddingLeft":"8px","backgroundColor":"#2a2d31","color":"#ffffff","borderRadius":"4px","fontSize":"11px","borderTopWidth":"1px","borderRightWidth":"1px","borderBottomWidth":"1px","borderLeftWidth":"1px","borderTopColor":"#4078ff","borderRightColor":"#4078ff","borderBottomColor":"#4078ff","borderLeftColor":"#4078ff","marginBottom":"6px"}],[[[0,"priority-selector"]],{"width":"100%","flexDirection":"row","justifyContent":"space-around"}],[[[0,"priority-option"]],{"flex":1,"height":"24px","textAlign":"center","lineHeight":"24px","borderRadius":"3px","fontSize":"11px","fontWeight":"bold","backgroundColor":"#333","color":"#ffffff"}],[[[0,"edit-actions"]],{"width":"100%","flexDirection":"row"}],[[[0,"empty-state"]],{"width":"100%","flex":1,"flexDirection":"column","justifyContent":"center","alignItems":"center"}],[[[0,"empty-text"]],{"fontSize":"12px","color":"#666666"}]]
var $app_script$ = function __scriptModule__(module, exports, $app_require$) {	"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.default = void 0;
var _default = exports.default = {
  data: {
    todos: [{
      id: 1,
      content: '完成项目文档',
      completed: false,
      priority: 'high'
    }, {
      id: 2,
      content: '代码审查',
      completed: true,
      priority: 'medium'
    }, {
      id: 3,
      content: '修复 bug',
      completed: false,
      priority: 'high'
    }, {
      id: 4,
      content: '性能优化',
      completed: false,
      priority: 'medium'
    }, {
      id: 5,
      content: '用户反馈收集',
      completed: true,
      priority: 'low'
    }],
    sortOrder: 'asc',
    editingIndex: -1,
    editingText: '',
    editingPriority: 'medium',
    nextId: 6,
    completedCount: 0,
    filteredTodos: []
  },
  // 生命周期钩子
  onInit() {
    console.log('页面初始化，当前任务数:', this.todos.length);
    // this.updateList();
  },
  onShow() {
    console.log('页面显示');
  },
  onHide() {
    console.log('页面隐藏');
  },
  onDestroy() {
    console.log('页面销毁');
  },
  // 更新列表数据和计数
  updateList() {
    // 计算已完成的任务数
    this.completedCount = this.todos.filter(item => item.completed).length;

    // 排序和过滤列表
    let filtered = this.todos.slice();
    filtered.sort((a, b) => {
      const aCompleted = a.completed ? 1 : 0;
      const bCompleted = b.completed ? 1 : 0;

      // 先按优先级排序
      const priorityOrder = {
        high: 3,
        medium: 2,
        low: 1
      };
      const priorityDiff = priorityOrder[b.priority] - priorityOrder[a.priority];
      if (priorityDiff !== 0) return priorityDiff;

      // 再按完成状态排序
      if (aCompleted !== bCompleted) {
        return this.sortOrder === 'asc' ? aCompleted - bCompleted : bCompleted - aCompleted;
      }
      return 0;
    });
    console.log('============ updateList  this.filteredTodos', this.filteredTodos);
    this.filteredTodos = filtered;
  },
  // 事件处理方法
  handleAddTask() {
    // 添加新任务
    this.todos.push({
      id: this.nextId++,
      content: '新任务 ' + this.nextId,
      completed: false,
      priority: 'medium'
    });
    this.updateList();
    console.log('任务已添加，当前总数:', this.todos.length);
  },
  handleUpdateTask() {
    console.log('更新任务');
  },
  handleStartEdit(index) {
    const item = this.todos[index];
    const realIndex = this.todos.findIndex(todo => todo.id === item.id);
    console.log(`~~~~ handleStartEdit index:${index} realIndex:${realIndex} item:${JSON.stringify(item)}}`);
    console.log(`~~~~ handleStartEdit 更新前 completed:${this.todos[index].completed}`);
    this.todos[index].completed = !this.todos[index].completed;
    console.log(`~~~~ handleStartEdit 更新后 completed:${this.todos[index].completed}`);
    this.todos[index].content = item.content.replace(/^\d+/, Date.now());
    console.log(`~~~~ this.todos[index]:${JSON.stringify(this.todos[index])} item:${JSON.stringify(item)}`);
    this.editingIndex = realIndex;
    this.editingText = item.content;
    this.editingPriority = item.priority;
  },
  handleDeleteTask(index) {
    const item = this.todos[index];
    const realIndex = this.todos.findIndex(todo => todo.id === item.id);
    this.todos.splice(realIndex, 1);
    this.updateList();
    console.log('任务已删除，当前总数:', this.todos.length);
  },
  handleClearCompleted() {
    this.todos = this.todos.filter(item => !item.completed);
    this.updateList();
    console.log('已清除完成任务，当前总数:', this.todos.length);
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
"__opts__":{"classList":["page","full","column"],
"style":{"backgroundColor":"#111518","paddingTop":"30px"}}}, [aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["header"]}}, [aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["stat-text"],
"value":function() { return (_vm_.todos.length) + " / " + (_vm_.completedCount) }}}, [])]),
aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["add-section"]}}, [aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["btn","btn-primary"],
"style":{"flex":"1"},
"events":{"click":function(evt) { return _vm_.handleAddTask(evt) }},
"value":"+"}}, []),
aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["btn","btn-primary"],
"style":{"flex":"1"},
"events":{"click":function(evt) { return _vm_.handleUpdateTask(evt) }},
"value":"修"}}, []),
aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["btn","btn-danger"],
"style":{"flex":"1"},
"events":{"click":function(evt) { return _vm_.handleClearCompleted(evt) }},
"value":"清"}}, [])]),
aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["list-container"]}}, [aiot.__ce__("list", {"__vm__":_vm_,
"__opts__":{"classList":["task-list"]}}, [aiot.__cf__({"__vm__":_vm_,
"__opts__":{"exp":function() { return _vm_.todos },
"key":"index",
"value":"item"}}, function(index, item, ){
          return [aiot.__ce__("list-item", {"__vm__":_vm_,
"__opts__":{"classList":["list-item-wrapper"]}}, [aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["task-item"],
"key":function() { return item.id }}}, [aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["content-area"],
"events":{"click":function(evt) { return _vm_.handleStartEdit(index, evt) }}}}, [aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["task-text"],
"style":function() {
        return $translateStyle$(item.completed?"text-decoration: line-through; color: #888;":"")
      },
"value":function() { return (item.content) + "-" + (item.completed?"\u5DF2\u5B8C\u6210":"\u672A\u5B8C\u6210") }}}, [])]),
aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["priority-dot"],
"style":function() {
        return $translateStyle$("background-color: " + (item.priority==="high"?"#ff4444":item.priority==="medium"?"#ffaa00":"#44aa44") + ";")
      }}}, []),
aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["action-btn","edit-btn"],
"events":{"click":function(evt) { return _vm_.handleStartEdit(index, evt) }},
"value":"✎"}}, []),
aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["action-btn","delete-btn"],
"events":{"click":function(evt) { return _vm_.handleDeleteTask(index, evt) }},
"value":"✕"}}, [])])])]
        })]),
aiot.__ci__({"__vm__":_vm_,
"__opts__":{"shown":function(){ return (_vm_.todos.length===0); }}}, function(){
          return [aiot.__ce__("div", {"__vm__":_vm_,
"__opts__":{"classList":["empty-state"]}}, [aiot.__ce__("text", {"__vm__":_vm_,
"__opts__":{"classList":["empty-text"],
"value":"暂无任务"}}, [])])]
        })])])

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