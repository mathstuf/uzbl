var util = (function () {
'use strict';

var slice = Array.prototype.slice;

var splitRegEx = /(?:\s+|("(?:\\\\.|[^"])*?"|'(?:\\\\.|[^\'])*?'))/;
var unquoteRegEx = /\\\\(.)/g;
var quoteCharacters = '\"\'';
var escapeLevels = [
    {
        level: 3,
        character: '\\'
    },
    {
        level: 2,
        character: '\''
    },
    {
        level: 2,
        character: '\"'
    },
    {
        level: 1,
        character: '@'
    }
]

var eventRegEx = /^EVENT \[[^\]]*\] (.*)$/;
var requestRegEx = /^REQUEST-([^ ]+) \[[^\]]*\] (.*)$/;

var isNotEmpty = function (str) {
    if (str === undefined) {
        return false;
    }
    return (str.length !== 0);
};
var removeQuotes = function (str) {
    if ((quoteCharacters.indexOf(str[0]) !== -1) &&
        str[str.length - 1] == str[0]) {
        return str.slice(1, -1);
    }
    return str;
};
var unquote = function (str) {
    return str.replace(unquoteRegEx, '\\1');
};
var split = function (line) {
    var args = line.split(splitRegEx).filter(isNotEmpty);
    args = args.map(removeQuotes).map(unquote);
    //var k = 0;
    //em.log('line: ' + line);
    //while (k < args.length) {
        //em.log('arg ' + k + ': ' + args[k]);
        //++k;
    //}
    return args;
};
var repeat = function (str, num) {
    return new Array(num + 1).join(str);
};

var eventHandlers = {
    callbacks: {},
    order: []
};
var requestHandlers = {
    callbacks: {},
    order: []
};

var addHandler = function (handlers, name, priority, evt, cb, ctx) {
    if (handlers.callbacks.hasOwnProperty(name)) {
        return false;
    }
    handlers.callbacks[name] = {
        evt: evt,
        callback: cb,
        context: ctx
    };
    handlers.order = handlers.order.concat({
        name: name,
        priority: priority
    });
    handlers.order.sort(function (a, b) {
        return a.priority - b.priority;
    });
    return true;
};
var removeHandler = function (handlers, name) {
    if (handlers.callbacks.hasOwnProperty(name)) {
        delete handlers.callbacks[name];
        handlers.order = handlers.filter(handlers.order, function (str) {
            return (str !== name);
        })
        return true;
    }
    return false;
};
var uzblEscape = function (str) {
    var i = 0;
    var chr;
    var repl;

    while (i < escapeLevels.length) {
        chr = escapeLevels[i].character;
        repl = repeat(chr, escapeLevels[i].level);
        ++i;

        str = str.replace(chr, repl)
    }

    return str;
};

return {
    /*
     * Built in dispatcher. Parses the event stream and calls the event callback
     */
    dispatch: function (line, eventCallback, requestCallback) {
        var spl;
        if (eventCallback !== undefined) {
            var spl = line.match(eventRegEx);
            if (spl !== null) {
                return eventCallback.apply(undefined, split(spl[1]));
            }
        }
        if (requestCallback !== undefined) {
            var spl = line.match(requestRegEx);
            if (spl !== null) {
                var args = [spl[1]].concat(split(spl[2]));
                return requestCallback.apply(undefined, args);
            }
        }

        return false;
    },
    uzblEscape: uzblEscape,
    expand: function (cmd, args) {
        var i = 0;
        var exp_cmd = '';
        var chr;

        while (i < cmd.length) {
            chr = cmd[i];
            i++;
            if (chr === '%') {
                if (i === cmd.length) {
                    break;
                }
                chr = cmd[i];
                i++;
                switch (chr) {
                case 's':
                    exp_cmd += args.join(' ');
                    break;
                case 'r':
                    exp_cmd += "\'" + uzblEscape(args.join(' ')) + "\'";
                    break;
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    exp_cmd += uzblEscape(args[chr - 1]);
                    break;
                case '%':
                default:
                    exp_cmd += chr;
                    break;
                }
            } else {
                exp_cmd += chr;
            }
        }

        return exp_cmd;
    },

    addEventHandler: function (name, priority, evt, cb, ctx) {
        addHandler(eventHandlers, name, priority, evt, cb, ctx);
    },
    removeEventHandler: function (name) {
        removeHandler(eventHandlers, name);
    },

    addRequestHandler: function (name, priority, evt, cb, ctx) {
        addHandler(requestHandlers, name, priority, evt, cb, ctx);
    },
    removeRequestHandler: function (name) {
        removeHandler(requestHandlers, name);
    },

    onEvent: function (_event, _eventArgs___) {
        var args = slice.apply(arguments);
        var evt = args.shift();
        var callbacks = eventHandlers.callbacks;
        var order = eventHandlers.order;
        var callback;
        var ret = false;
        var i = 0;

        while (i < order.length) {
            callback = callbacks[order[i].name];
            ++i;
            if ((callback.evt === true) ||
                (callback.evt === evt)) {
                callback.callback.apply(callback.context, args);
                ret = true;
            }
        }

        return ret;
    },
    onRequest: function (_cookie, _request, _requestArgs___) {
        var args = slice.apply(arguments);
        var cookie = args.shift();
        var request = args.shift();
        var callbacks = requestHandlers.callbacks;
        var order = requestHandlers.order;
        var i = 0;
        var callback;

        var response = {
            response: undefined,
            request: request,
            args: args,
            kwargs: {}
        };

        while (i < order.length) {
            callback = callbacks[order[i].name];
            ++i;
            if ((callback.evt === true) ||
                (callback.evt === request)) {
                callback.callback.apply(callback.context, response);
            }
        }

        if (response.response === undefined) {
            return false;
        }

        em.reply(cookie, response.response);
        return true;
    }
};
})();
