// Simple CodeMirror mode for Toka
CodeMirror.defineSimpleMode("toka", {
    start: [
      {regex: /"(?:[^\\]|\\.)*?(?:"|$)/, token: "string"},
      {regex: /s"(?:[^\\]|\\.)*?(?:"|$)/, token: "string"}, // string view
      {regex: /(?:fn|auto|shape|impl|import|guard|cede|new|unsafe|alloc|free|null|none|effects|return|if|else|while|loop|for|match|in|break|continue|as|pub|async|await)\b/, token: "keyword"},
      {regex: /(?:i8|i16|i32|i64|u8|u16|u32|u64|f32|f64|bool|char|void|String|Vec|Result|Option|Data|Buffer|Node|Point)\b/, token: "type"},
      {regex: /(?:true|false)\b/, token: "atom"},
      {regex: /0x[a-f\d]+|[-+]?(?:\.\d+|\d+\.?\d*)(?:e[-+]?\d+)?/i, token: "number"},
      {regex: /\/\/.*/, token: "comment"},
      {regex: /\/\*/, token: "comment", next: "comment"},
      {regex: /#|\?|\^|~|\*|&|!|<-/, token: "operator"},
      {regex: /[a-zA-Z_][a-zA-Z0-9_]*/, token: "variable"},
    ],
    comment: [
      {regex: /.*?\*\//, token: "comment", next: "start"},
      {regex: /.*/, token: "comment"}
    ],
    meta: {
      dontIndentStates: ["comment"],
      lineComment: "//",
      blockCommentStart: "/*",
      blockCommentEnd: "*/"
    }
  });
