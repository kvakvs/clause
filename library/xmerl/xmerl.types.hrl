%% OTP 29 xmerl types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([element/0,
              xmlAttribute/0,
              xmlComment/0,
              xmlDecl/0,
              xmlDocument/0,
              xmlElement/0,
              xmlNamespace/0,
              xmlNsNode/0,
              xmlPI/0,
              xmlText/0]).
-record(xmlDecl,{vsn, encoding, standalone, attributes}).
-record(xmlAttribute,{name,
                      expanded_name = [],
                      nsinfo = [],
                      namespace = [],
                      parents = [],
                      pos,
                      language = [],
                      value, normalized}).
-record(xmlNamespace,{default = [], nodes = []}).
-record(xmlNsNode,{parents = [], pos, prefix, uri = []}).
-record(xmlElement,{name,
                    expanded_name = [],
                    nsinfo = [],
                    namespace = #xmlNamespace{},
                    parents = [],
                    pos,
                    attributes = [],
                    content = [],
                    language = "",
                    xmlbase = "",
                    elementdef = undeclared}).
-record(xmlText,{parents = [], pos, language = [], value, type = text}).
-record(xmlComment,{parents = [], pos, language = [], value}).
-record(xmlPI,{name, parents = [], pos, value}).
-record(xmlDocument,{content}).
-type xmlDecl() :: #xmlDecl{}.
-type xmlAttribute() :: #xmlAttribute{}.
-type xmlNamespace() :: #xmlNamespace{}.
-type xmlNsNode() :: #xmlNsNode{}.
-type xmlElement() :: #xmlElement{}.
-type xmlText() :: #xmlText{}.
-type xmlComment() :: #xmlComment{}.
-type xmlPI() :: #xmlPI{}.
-type xmlDocument() :: #xmlDocument{}.
-type element() ::
          xmlText() | xmlElement() | xmlPI() | xmlComment() | xmlDecl().
