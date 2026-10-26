%% asn1ct's Kitchen.hrl for tests/fixtures/executables/asn1_ber/Kitchen.asn (OTP 29.1.1), without its header comment:
%% the records and macros clau's generated header must reproduce (plan 21 step LIB84).

-ifndef(_KITCHEN_HRL_).
-define(_KITCHEN_HRL_, true).

-record('Wrapper', {
  version,
  body
}).

-record('Record', {
  id,
  name = asn1_NOVALUE,
  level = asn1_DEFAULT,
  flag = asn1_DEFAULT,
  inner = asn1_NOVALUE
}).

-record('Record_inner', {
  a,
  b = asn1_NOVALUE
}).

-record('Extended', {
  first,
  %% with extensions
  second,
  third,
  fourth = asn1_NOVALUE,
  %% end of extensions
  last
}).

-record('Unordered', {
  z,
  a,
  m = asn1_NOVALUE
}).

-record('Pairs_SEQOF', {
  key,
  value
}).

-record('Tagged', {
  impl,
  expl,
  priv,
  deep,
  choice
}).

-record('Tree', {
  value,
  children = asn1_NOVALUE
}).

-record('Holder', {
  anything,
  defined,
  id
}).

-record('Attribute', {
  type,
  value
}).

-record('ClosedAttribute', {
  type,
  value
}).

-record('MultiValued', {
  type,
  values
}).

-record('WrappedRecord', {
  version,
  body
}).

-record('WrappedList_SEQOF', {
  version,
  body
}).

-record('INSTANCE OF', {
  'type-id',
  value
}).

-define('defaultLevel', 10).
-define('someOid', {1,2,840}).
-define('aRecord', {'Record',7,[115,101,118,101,110],5,true,asn1_NOVALUE}).
-endif. %% _KITCHEN_HRL_
