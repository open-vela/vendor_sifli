export default function(global, globalThis, window, $app_exports$, $app_evaluate$) {
    var org_app_require = $app_require$;
    (function(global, globalThis, window, $app_exports$, $app_evaluate$) {
        var setTimeout = global.setTimeout;
        var setInterval = global.setInterval;
        var clearTimeout = global.clearTimeout;
        var clearInterval = global.clearInterval;
        var $app_require$1 = global.$app_require$ || org_app_require;
        var createPageHandler = function() {
            return (()=>{
                var __webpack_modules__ = {};
                var __webpack_module_cache__ = {};
                function __webpack_require__(moduleId) {
                    var cachedModule = __webpack_module_cache__[moduleId];
                    if (void 0 !== cachedModule) return cachedModule.exports;
                    var module = __webpack_module_cache__[moduleId] = {
                        exports: {}
                    };
                    __webpack_modules__[moduleId](module, module.exports, __webpack_require__);
                    return module.exports;
                }
                (()=>{
                    __webpack_require__.g = (()=>{
                        if ('object' == typeof globalThis) return globalThis;
                        try {
                            return this || new Function('return this')();
                        } catch (e) {
                            if ('object' == typeof window) return window;
                        }
                    })();
                })();
                (()=>{
                    __webpack_require__.rv = ()=>"1.6.6";
                })();
                (()=>{
                    __webpack_require__.ruid = "bundler=rspack@1.6.6";
                })();
                var $app_style$ = [
                    [
                        {
                            condition: "screen and (shape:circle)"
                        },
                        [
                            [
                                0,
                                "resultBox"
                            ]
                        ],
                        {
                            width: "400px",
                            height: "155px"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:circle)"
                        },
                        [
                            [
                                0,
                                "resultShow"
                            ]
                        ],
                        {
                            width: "400px",
                            height: "50px",
                            marginTop: "105px"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:circle)"
                        },
                        [
                            [
                                0,
                                "keyBoardBox"
                            ]
                        ],
                        {
                            marginTop: "8px",
                            flexDirection: "row",
                            justifyContent: "center",
                            alignItems: "center",
                            width: "420px",
                            height: "60px"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:circle)"
                        },
                        [
                            [
                                0,
                                "inputCommon"
                            ]
                        ],
                        {
                            width: "74px",
                            height: "54px",
                            marginLeft: "3px",
                            marginRight: "3px"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:circle)"
                        },
                        [
                            [
                                0,
                                "clearAll"
                            ]
                        ],
                        {
                            width: "74px",
                            height: "54px",
                            marginTop: "-327px",
                            marginLeft: "-318px",
                            borderTopColor: "#000",
                            borderRightColor: "#000",
                            borderBottomColor: "#000",
                            borderLeftColor: "#000"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:circle)"
                        },
                        [
                            [
                                0,
                                "input1"
                            ]
                        ],
                        {
                            backgroundColor: "#f53333"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:circle)"
                        },
                        [
                            [
                                0,
                                "delIcon"
                            ]
                        ],
                        {
                            width: "30px",
                            height: "22px",
                            marginLeft: "320px",
                            marginTop: "28px"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:rect)"
                        },
                        [
                            [
                                0,
                                "container"
                            ]
                        ],
                        {
                            paddingBottom: "5px"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:rect)"
                        },
                        [
                            [
                                0,
                                "resultBox"
                            ]
                        ],
                        {
                            width: "430px",
                            height: "18%"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:rect)"
                        },
                        [
                            [
                                0,
                                "resultShow"
                            ]
                        ],
                        {
                            width: "100%",
                            height: "100%"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:rect)"
                        },
                        [
                            [
                                0,
                                "keyBoardBox"
                            ]
                        ],
                        {
                            width: "430px",
                            flex: 1,
                            marginTop: "8px",
                            paddingTop: "0",
                            paddingRight: "0",
                            paddingBottom: "0",
                            paddingLeft: "0",
                            justifyContent: "center"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:rect)"
                        },
                        [
                            [
                                0,
                                "inputCommon"
                            ]
                        ],
                        {
                            width: "24%",
                            height: "100%",
                            marginLeft: "3px",
                            marginRight: "3px",
                            marginBottom: "0px",
                            backgroundColor: "#3d3d3d",
                            color: "#ffffff",
                            borderRadius: "20px",
                            textAlign: "center"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:rect)"
                        },
                        [
                            [
                                0,
                                "input1"
                            ]
                        ],
                        {
                            color: "#ff6923"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:pill-shaped)"
                        },
                        [
                            [
                                0,
                                "container"
                            ]
                        ],
                        {
                            paddingTop: "20px",
                            paddingRight: "0px",
                            paddingBottom: "20px",
                            paddingLeft: "0px"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:pill-shaped)"
                        },
                        [
                            [
                                0,
                                "resultBox"
                            ]
                        ],
                        {
                            width: "100%",
                            height: "18%",
                            paddingRight: "20px",
                            borderTopColor: "green",
                            borderRightColor: "green",
                            borderBottomColor: "green",
                            borderLeftColor: "green",
                            borderStyle: "solid",
                            borderTopWidth: "1px",
                            borderRightWidth: "1px",
                            borderBottomWidth: "1px",
                            borderLeftWidth: "1px"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:pill-shaped)"
                        },
                        [
                            [
                                0,
                                "resultShow"
                            ]
                        ],
                        {
                            width: "100%",
                            height: "100%",
                            fontSize: "36px",
                            borderTopColor: "yellow",
                            borderRightColor: "yellow",
                            borderBottomColor: "yellow",
                            borderLeftColor: "yellow",
                            borderStyle: "solid",
                            borderTopWidth: "1px",
                            borderRightWidth: "1px",
                            borderBottomWidth: "1px",
                            borderLeftWidth: "1px"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:pill-shaped)"
                        },
                        [
                            [
                                0,
                                "result-text"
                            ]
                        ],
                        {
                            fontSize: "60px",
                            lineHeight: "60px"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:pill-shaped)"
                        },
                        [
                            [
                                0,
                                "keyBoardBox"
                            ]
                        ],
                        {
                            width: "100%",
                            height: "140px",
                            marginTop: "8px",
                            paddingTop: "0",
                            paddingRight: "0",
                            paddingBottom: "0",
                            paddingLeft: "0",
                            justifyContent: "center",
                            borderTopColor: "red",
                            borderRightColor: "red",
                            borderBottomColor: "red",
                            borderLeftColor: "red",
                            borderStyle: "solid",
                            borderTopWidth: "1px",
                            borderRightWidth: "1px",
                            borderBottomWidth: "1px",
                            borderLeftWidth: "1px"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:pill-shaped)"
                        },
                        [
                            [
                                0,
                                "inputCommon"
                            ]
                        ],
                        {
                            width: "24%",
                            height: "100%",
                            marginLeft: "3px",
                            marginRight: "3px",
                            marginBottom: "0px",
                            backgroundColor: "#3d3d3d",
                            color: "#ffffff",
                            borderRadius: "20px",
                            textAlign: "center",
                            borderTopColor: "pink",
                            borderRightColor: "pink",
                            borderBottomColor: "pink",
                            borderLeftColor: "pink",
                            borderStyle: "solid",
                            borderTopWidth: "1px",
                            borderRightWidth: "1px",
                            borderBottomWidth: "1px",
                            borderLeftWidth: "1px"
                        }
                    ],
                    [
                        {
                            condition: "screen and (shape:pill-shaped)"
                        },
                        [
                            [
                                0,
                                "input1"
                            ]
                        ],
                        {
                            color: "#ff6923"
                        }
                    ],
                    [
                        [
                            [
                                0,
                                "container"
                            ]
                        ],
                        {
                            width: "100%",
                            height: "100%",
                            flexDirection: "column",
                            alignItems: "center"
                        }
                    ],
                    [
                        [
                            [
                                0,
                                "resultShow"
                            ]
                        ],
                        {
                            color: "rgb(254, 243, 232)",
                            fontSize: "40px",
                            textAlign: "right"
                        }
                    ]
                ];
                var $app_script$ = function __scriptModule__(module, exports, $app_require$1) {
                    "use strict";
                    Object.defineProperty(exports, "__esModule", {
                        value: true
                    });
                    exports.default = void 0;
                    var _system = _interopRequireDefault($app_require$1("@app-module/system.app"));
                    var _system2 = _interopRequireDefault($app_require$1("@app-module/system.device"));
                    function _interopRequireDefault(e) {
                        return e && e.__esModule ? e : {
                            default: e
                        };
                    }
                    const MinusIcon = '+/-';
                    const DelIcon = ' ';
                    var _default = exports.default = {
                        data: {
                            resultShow: '0',
                            keyBoardNum: [
                                'C',
                                '7',
                                '8',
                                '9',
                                MinusIcon,
                                DelIcon,
                                '4',
                                '5',
                                '6',
                                '+',
                                'x',
                                '1',
                                '2',
                                '3',
                                '-',
                                '/',
                                '0',
                                '.',
                                '=',
                                '%'
                            ],
                            commonColorList: [
                                '#F53333',
                                '#505050',
                                '#505050',
                                '#505050',
                                '#505050',
                                '#FFA626',
                                '#505050',
                                '#505050',
                                '#505050',
                                '#FFA626',
                                '#FFA626',
                                '#505050',
                                '#505050',
                                '#505050',
                                '#FFA626',
                                '#FFA626',
                                '#505050',
                                '#505050',
                                '#FFA626'
                            ],
                            clickColorList: [],
                            signColorList: [
                                '#FFFFFF',
                                '#FFFFFF',
                                '#FFFFFF',
                                '#FFFFFF'
                            ]
                        },
                        onInit () {
                            const that = this;
                            this.clickColorList = this.commonColorList.slice();
                        },
                        exit () {
                            _system.default.terminate();
                        }
                    };
                    const moduleOwn = exports.default || module.exports;
                    const accessors = [
                        'public',
                        'protected',
                        'private'
                    ];
                    if (moduleOwn.data && accessors.some(function(acc) {
                        return moduleOwn[acc];
                    })) throw new Error('页面VM对象中的属性data不可与"' + accessors.join(',') + '"同时存在，请使用private替换data名称');
                    if (!moduleOwn.data) {
                        moduleOwn.data = {};
                        moduleOwn._descriptor = {};
                        accessors.forEach(function(acc) {
                            const accType = typeof moduleOwn[acc];
                            if ('object' === accType) {
                                moduleOwn.data = Object.assign(moduleOwn.data, moduleOwn[acc]);
                                for(const name in moduleOwn[acc])moduleOwn._descriptor[name] = {
                                    access: acc
                                };
                            } else if ('function' === accType) console.warn('页面VM对象中的属性' + acc + '的值不能是函数，请使用对象');
                        });
                    }
                };
                var $app_template$ = function(vm) {
                    const _vm_ = vm || this;
                    return aiot.__ce__("div", {
                        __vm__: _vm_,
                        __opts__: {
                            classList: [
                                "container"
                            ],
                            events: {
                                swipe: function(evt) {
                                    return _vm_.touchMove(evt);
                                }
                            }
                        }
                    }, [
                        aiot.__ci__({
                            __vm__: _vm_,
                            __opts__: {
                                shown: function() {
                                    return "circle" === _vm_.$app.$def.screenShape;
                                }
                            }
                        }, function() {
                            return [
                                aiot.__ce__("div", {
                                    __vm__: _vm_,
                                    __opts__: {
                                        classList: [
                                            "container"
                                        ]
                                    }
                                }, [
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "resultBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "resultShow"
                                                ],
                                                value: function() {
                                                    return _vm_.resultShow;
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[1] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[1];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[2] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[2];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[3] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[3];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[4] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[4];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[5] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[5];
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[6] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[6];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[7] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[7];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[8] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[8];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[9] + ";color:" + _vm_.signColorList[3]);
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[9];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[10] + ";color:" + _vm_.signColorList[1]);
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[10];
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[11] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[11];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[12] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[12];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[13] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[13];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[14] + ";color:" + _vm_.signColorList[2]);
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[14];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[15] + ";color:" + _vm_.signColorList[0]);
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[15];
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[16] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[16];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[17] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[17];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                type: "button",
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                style: function() {
                                                    return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[18] + ";");
                                                },
                                                value: function() {
                                                    return _vm_.keyBoardNum[18];
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("input", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            type: "button",
                                            classList: [
                                                "clearAll"
                                            ],
                                            style: function() {
                                                return __webpack_require__.g.$translateStyle$("background-color: " + _vm_.clickColorList[0] + ";");
                                            },
                                            value: function() {
                                                return _vm_.keyBoardNum[0];
                                            }
                                        }
                                    }, []),
                                    aiot.__ce__("image", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            src: "/res/textures/del.png",
                                            classList: [
                                                "delIcon"
                                            ]
                                        }
                                    }, [])
                                ])
                            ];
                        }),
                        aiot.__ci__({
                            __vm__: _vm_,
                            __opts__: {
                                shown: function() {
                                    return "rect" === _vm_.$app.$def.screenShape;
                                }
                            }
                        }, function() {
                            return [
                                aiot.__ce__("div", {
                                    __vm__: _vm_,
                                    __opts__: {
                                        classList: [
                                            "container"
                                        ]
                                    }
                                }, [
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "resultBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "resultShow"
                                                ],
                                                value: function() {
                                                    return _vm_.resultShow;
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: "AC"
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: "D"
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[19];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[15];
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[1];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[2];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[3];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[10];
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[6];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[7];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[8];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[14];
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[11];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[12];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[13];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[9];
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                events: {
                                                    click: function(evt) {
                                                        return _vm_.exit(evt);
                                                    }
                                                },
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: "退出"
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[16];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[17];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[18];
                                                }
                                            }
                                        }, [])
                                    ])
                                ])
                            ];
                        }),
                        aiot.__ci__({
                            __vm__: _vm_,
                            __opts__: {
                                shown: function() {
                                    return "pill-shaped" === _vm_.$app.$def.screenShape;
                                }
                            }
                        }, function() {
                            return [
                                aiot.__ce__("div", {
                                    __vm__: _vm_,
                                    __opts__: {
                                        classList: [
                                            "container"
                                        ]
                                    }
                                }, [
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "resultBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "resultShow",
                                                    "result-text"
                                                ],
                                                value: function() {
                                                    return _vm_.resultShow;
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: "AC"
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: "D"
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[19];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[15];
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[1];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[2];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[3];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[10];
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[6];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[7];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[8];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[14];
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[11];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[12];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[13];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[9];
                                                }
                                            }
                                        }, [])
                                    ]),
                                    aiot.__ce__("div", {
                                        __vm__: _vm_,
                                        __opts__: {
                                            classList: [
                                                "keyBoardBox"
                                            ]
                                        }
                                    }, [
                                        aiot.__ce__("input", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                events: {
                                                    click: function(evt) {
                                                        return _vm_.exit(evt);
                                                    }
                                                },
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: "退出"
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[16];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[17];
                                                }
                                            }
                                        }, []),
                                        aiot.__ce__("text", {
                                            __vm__: _vm_,
                                            __opts__: {
                                                classList: [
                                                    "inputCommon",
                                                    "input1"
                                                ],
                                                value: function() {
                                                    return _vm_.keyBoardNum[18];
                                                }
                                            }
                                        }, [])
                                    ])
                                ])
                            ];
                        })
                    ]);
                };
                $app_exports$['entry'] = function($app_exports$) {
                    $app_script$({}, $app_exports$, $app_require$1);
                    $app_exports$.default.template = $app_template$;
                    $app_exports$.default.style = $app_style$;
                };
            })();
        };
        return createPageHandler();
    })(global, globalThis, window, $app_exports$, $app_evaluate$);
}
