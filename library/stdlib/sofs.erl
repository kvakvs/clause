%% Clause's sofs (plan 21 step LIB7A, docs/builtins.md#sets-of-sets): the data operations are runtime builtins with
%% clause_builtin_placeholder bodies; the functions taking funs are Erlang over clause_sofs builtins.
-module(sofs).
-export([
    from_term/1,
    from_term/2,
    from_external/2,
    empty_set/0,
    is_type/1,
    set/1,
    set/2,
    from_sets/1,
    relation/1,
    relation/2,
    a_function/1,
    a_function/2,
    family/1,
    family/2,
    to_external/1,
    type/1,
    to_sets/1,
    no_elements/1,
    specification/2,
    union/2,
    intersection/2,
    difference/2,
    symdiff/2,
    symmetric_partition/2,
    product/1,
    product/2,
    constant_function/2,
    is_equal/2,
    is_subset/2,
    is_sofs_set/1,
    is_set/1,
    is_empty_set/1,
    is_disjoint/2,
    union/1,
    intersection/1,
    canonical_relation/1,
    relation_to_family/1,
    domain/1,
    range/1,
    field/1,
    relative_product/1,
    relative_product/2,
    relative_product1/2,
    converse/1,
    image/2,
    inverse_image/2,
    strict_relation/1,
    weak_relation/1,
    extension/3,
    is_a_function/1,
    composite/2,
    inverse/1,
    restriction/2,
    restriction/3,
    drestriction/2,
    drestriction/3,
    substitution/2,
    projection/2,
    partition/1,
    partition/2,
    partition/3,
    multiple_relative_product/2,
    join/4,
    family_to_relation/1,
    family_specification/2,
    union_of_family/1,
    intersection_of_family/1,
    family_union/1,
    family_intersection/1,
    family_domain/1,
    family_range/1,
    family_field/1,
    family_union/2,
    family_intersection/2,
    family_difference/2,
    partition_family/2,
    family_projection/2,
    family_to_digraph/1,
    family_to_digraph/2,
    digraph_to_family/1,
    digraph_to_family/2,
    fam2rel/1,
    rel2fam/1
]).
-include("sofs.types.hrl").

% Native from_term/1 data operation.
from_term(_) -> clause_builtin_placeholder.

% Native from_term/2 data operation.
from_term(_, _) -> clause_builtin_placeholder.

% Native from_external/2 data operation.
from_external(_, _) -> clause_builtin_placeholder.

% Native empty_set/0 data operation.
empty_set() -> clause_builtin_placeholder.

% Native is_type/1 data operation.
is_type(_) -> clause_builtin_placeholder.

% Native set/1 data operation.
set(_) -> clause_builtin_placeholder.

% Native set/2 data operation.
set(_, _) -> clause_builtin_placeholder.

% Native from_sets/1 data operation.
from_sets(_) -> clause_builtin_placeholder.

% Native relation/1 data operation.
relation(_) -> clause_builtin_placeholder.

% Native relation/2 data operation.
relation(_, _) -> clause_builtin_placeholder.

% Native a_function/1 data operation.
a_function(_) -> clause_builtin_placeholder.

% Native a_function/2 data operation.
a_function(_, _) -> clause_builtin_placeholder.

% Native family/1 data operation.
family(_) -> clause_builtin_placeholder.

% Native family/2 data operation.
family(_, _) -> clause_builtin_placeholder.

% Native to_external/1 data operation.
to_external(_) -> clause_builtin_placeholder.

% Native type/1 data operation.
type(_) -> clause_builtin_placeholder.

% Native to_sets/1 data operation.
to_sets(_) -> clause_builtin_placeholder.

% Native no_elements/1 data operation.
no_elements(_) -> clause_builtin_placeholder.

% Native union/2 data operation.
union(_, _) -> clause_builtin_placeholder.

% Native intersection/2 data operation.
intersection(_, _) -> clause_builtin_placeholder.

% Native difference/2 data operation.
difference(_, _) -> clause_builtin_placeholder.

% Native symdiff/2 data operation.
symdiff(_, _) -> clause_builtin_placeholder.

% Native symmetric_partition/2 data operation.
symmetric_partition(_, _) -> clause_builtin_placeholder.

% Native product/1 data operation.
product(_) -> clause_builtin_placeholder.

% Native product/2 data operation.
product(_, _) -> clause_builtin_placeholder.

% Native constant_function/2 data operation.
constant_function(_, _) -> clause_builtin_placeholder.

% Native is_equal/2 data operation.
is_equal(_, _) -> clause_builtin_placeholder.

% Native is_subset/2 data operation.
is_subset(_, _) -> clause_builtin_placeholder.

% Native is_sofs_set/1 data operation.
is_sofs_set(_) -> clause_builtin_placeholder.

% Native is_set/1 data operation.
is_set(_) -> clause_builtin_placeholder.

% Native is_empty_set/1 data operation.
is_empty_set(_) -> clause_builtin_placeholder.

% Native is_disjoint/2 data operation.
is_disjoint(_, _) -> clause_builtin_placeholder.

% Native union/1 data operation.
union(_) -> clause_builtin_placeholder.

% Native intersection/1 data operation.
intersection(_) -> clause_builtin_placeholder.

% Native canonical_relation/1 data operation.
canonical_relation(_) -> clause_builtin_placeholder.

% Native relation_to_family/1 data operation.
relation_to_family(_) -> clause_builtin_placeholder.

% Native domain/1 data operation.
domain(_) -> clause_builtin_placeholder.

% Native range/1 data operation.
range(_) -> clause_builtin_placeholder.

% Native field/1 data operation.
field(_) -> clause_builtin_placeholder.

% Native relative_product/1 data operation.
relative_product(_) -> clause_builtin_placeholder.

% Native relative_product/2 data operation.
relative_product(_, _) -> clause_builtin_placeholder.

% Native relative_product1/2 data operation.
relative_product1(_, _) -> clause_builtin_placeholder.

% Native converse/1 data operation.
converse(_) -> clause_builtin_placeholder.

% Native image/2 data operation.
image(_, _) -> clause_builtin_placeholder.

% Native inverse_image/2 data operation.
inverse_image(_, _) -> clause_builtin_placeholder.

% Native strict_relation/1 data operation.
strict_relation(_) -> clause_builtin_placeholder.

% Native weak_relation/1 data operation.
weak_relation(_) -> clause_builtin_placeholder.

% Native extension/3 data operation.
extension(_, _, _) -> clause_builtin_placeholder.

% Native is_a_function/1 data operation.
is_a_function(_) -> clause_builtin_placeholder.

% Native composite/2 data operation.
composite(_, _) -> clause_builtin_placeholder.

% Native inverse/1 data operation.
inverse(_) -> clause_builtin_placeholder.

% Native restriction/2 data operation.
restriction(_, _) -> clause_builtin_placeholder.

% Native drestriction/2 data operation.
drestriction(_, _) -> clause_builtin_placeholder.

% Native partition/1 data operation.
partition(_) -> clause_builtin_placeholder.

% Native multiple_relative_product/2 data operation.
multiple_relative_product(_, _) -> clause_builtin_placeholder.

% Native join/4 data operation.
join(_, _, _, _) -> clause_builtin_placeholder.

% Native family_to_relation/1 data operation.
family_to_relation(_) -> clause_builtin_placeholder.

% Native union_of_family/1 data operation.
union_of_family(_) -> clause_builtin_placeholder.

% Native intersection_of_family/1 data operation.
intersection_of_family(_) -> clause_builtin_placeholder.

% Native family_union/1 data operation.
family_union(_) -> clause_builtin_placeholder.

% Native family_intersection/1 data operation.
family_intersection(_) -> clause_builtin_placeholder.

% Native family_domain/1 data operation.
family_domain(_) -> clause_builtin_placeholder.

% Native family_range/1 data operation.
family_range(_) -> clause_builtin_placeholder.

% Native family_field/1 data operation.
family_field(_) -> clause_builtin_placeholder.

% Native family_union/2 data operation.
family_union(_, _) -> clause_builtin_placeholder.

% Native family_intersection/2 data operation.
family_intersection(_, _) -> clause_builtin_placeholder.

% Native family_difference/2 data operation.
family_difference(_, _) -> clause_builtin_placeholder.

% Native fam2rel/1 data operation.
fam2rel(_) -> clause_builtin_placeholder.

% Native rel2fam/1 data operation.
rel2fam(_) -> clause_builtin_placeholder.

% Select elements by a callback on either their external or wrapped representation.
specification(Fun, {'Set', Data, Type}) ->
    Selected = select_values(Data, Fun, member_type(Type), external(Fun)),
    sofs:from_external(Selected, [Type]).

% A set element's wrapper uses the member type when the element is itself a set.
member_type([Type]) -> Type;
member_type(Type) -> Type.

% Only a non-atom second component activates the external callback convention.
external({external, Fun}) when not is_atom(Fun) -> {yes, Fun};
external(_) -> no.

% Invoke one predicate and enforce a boolean result.
predicate(Fun, Value) ->
    case Fun(Value) of
        true -> true;
        false -> false;
        _ -> erlang:error(badarg)
    end.

% Walk predicate inputs in set order, retaining the original external values.
select_values([], _, _, _) ->
    [];
select_values([Value | Rest], Fun, Type, Mode) ->
    Keep =
        case Mode of
            no -> predicate(Fun, clause_sofs:element(Value, Type));
            {yes, External} -> predicate(External, Value)
        end,
    case Keep of
        true -> [Value | select_values(Rest, Fun, Type, Mode)];
        false -> select_values(Rest, Fun, Type, Mode)
    end.

% Integer selectors run entirely in the native helper.
substitution(Index, Set) when is_integer(Index), is_record(Set, 'Set', 3) ->
    clause_sofs:substitution(Index, Set);
substitution(Fun, {'Set', _, _} = Set) ->
    transform(substitution, Fun, Set).

% A fun projection is the range of its substitution relation.
projection(Index, Set) when is_integer(Index), is_record(Set, 'Set', 3) ->
    clause_sofs:projection(Index, Set);
projection(Fun, Set) ->
    sofs:range(sofs:substitution(Fun, Set)).

% Partition groups elements by equal callback results.
partition(Index, Set) when is_integer(Index), is_record(Set, 'Set', 3) ->
    clause_sofs:partition(Index, Set);
partition(Fun, Set) ->
    sofs:range(sofs:partition_family(Fun, Set)).

% A family partition retains the callback result as each group's key.
partition_family(Index, Set) when is_integer(Index), is_record(Set, 'Set', 3) ->
    clause_sofs:partition_family(Index, Set);
partition_family(Fun, {'Set', _, _} = Set) ->
    transform(partition_family, Fun, Set).

% Empty internal transforms have no inferred output type; external transforms still probe explicit types.
transform(Operation, Fun, {'Set', Data, Type} = Set) ->
    case external(Fun) of
        no when Data =:= [] -> sofs:empty_set();
        no ->
            {Pairs, NewType} = internal_pairs(Data, Fun, member_type(Type), '_'),
            clause_sofs:finish(Operation, Set, Pairs, NewType);
        {yes, _} when Type =:= '_' -> sofs:empty_set();
        {yes, _} when is_list(Type) -> erlang:error(badarg);
        {yes, External} ->
            external_transform(Operation, External, Set)
    end.

% Probe the type outside the protected data transformation, preserving callback exceptions there.
external_transform(Operation, Fun, {'Set', Data, Type} = Set) ->
    NewType = Fun(Type),
    try validate_external(Fun, Type, NewType) of
        Sort ->
            Pairs = external_pairs(Data, Fun),
            clause_sofs:finish_external(Operation, Set, Pairs, NewType, Sort)
    catch
        _:_ -> erlang:error(badarg)
    end.

% Validate the result type before calling the numbered order probe.
validate_external(Fun, Type, NewType) ->
    true = sofs:is_type(NewType),
    clause_sofs:order_probe(Fun(clause_sofs:numbered(Type))).

% Invoke a wrapped SetFun and let the native helper merge its returned type.
internal_pairs([], _, _, NewType) ->
    {[], NewType};
internal_pairs([Value | Rest], Fun, Type, Previous) ->
    Result = Fun(clause_sofs:element(Value, Type)),
    {External, NewType} = clause_sofs:accept(Result, Previous),
    {Tail, FinalType} = internal_pairs(Rest, Fun, Type, NewType),
    {[{Value, External} | Tail], FinalType}.

% External callbacks receive values directly, after type and order probing.
external_pairs([], _) ->
    [];
external_pairs([Value | Rest], Fun) ->
    Result = Fun(Value),
    [{Value, Result} | external_pairs(Rest, Fun)].

% Restriction by a coordinate or by a callback result.
restriction(Index, Set, Keys) when
    is_integer(Index), is_record(Set, 'Set', 3), is_record(Keys, 'Set', 3)
->
    clause_sofs:restriction(Index, Set, Keys);
restriction(Fun, {'Set', _, _} = Set, {'Set', _, _} = Keys) ->
    filter_transform(restriction, Fun, Set, Keys).

% Complementary restriction uses the same callback order and type checks.
drestriction(Index, Set, Keys) when
    is_integer(Index), is_record(Set, 'Set', 3), is_record(Keys, 'Set', 3)
->
    clause_sofs:drestriction(Index, Set, Keys);
drestriction(Fun, {'Set', _, _} = Set, {'Set', _, _} = Keys) ->
    filter_transform(drestriction, Fun, Set, Keys).

% Return both halves using one callback pass.
partition(Index, Set, Keys) when
    is_integer(Index), is_record(Set, 'Set', 3), is_record(Keys, 'Set', 3)
->
    clause_sofs:partition(Index, Set, Keys);
partition(Fun, {'Set', _, _} = Set, {'Set', _, _} = Keys) ->
    filter_transform(partition, Fun, Set, Keys).

% Untyped keys short circuit only internal functions; untyped sources short circuit external ones.
filter_transform(Operation, Fun, {'Set', Data, Type} = Set, {'Set', _, KeyType} = Keys) ->
    case external(Fun) of
        no when KeyType =:= '_' -> empty_filter(Operation, Set, Keys);
        no ->
            {Pairs, NewType} = internal_pairs(Data, Fun, member_type(Type), '_'),
            Mapping = clause_sofs:finish(substitution, Set, Pairs, NewType),
            clause_sofs:filter(Operation, Mapping, Keys);
        {yes, _} when Type =:= '_' -> empty_filter(Operation, Set, Set);
        {yes, _} when is_list(Type) -> erlang:error(badarg);
        {yes, External} ->
            external_filter(Operation, External, Set, Keys)
    end.

% Preserve the exact short-circuit record types on both sides of a partition.
empty_filter(restriction, _, Empty) -> Empty;
empty_filter(drestriction, Set, _) -> Set;
empty_filter(partition, Set, Empty) -> {Empty, Set}.

% External restriction catches failures after its initial type call.
external_filter(Operation, Fun, {'Set', Data, Type} = Set, {'Set', _, KeyType} = Keys) ->
    NewType = Fun(Type),
    try validate_external(Fun, Type, NewType) of
        Sort ->
            clause_sofs:match_types(NewType, KeyType),
            Mapping = clause_sofs:finish(substitution, Set, external_pairs(Data, Fun), NewType),
            clause_sofs:filter_external(Operation, Mapping, Keys, Sort)
    catch
        _:_ -> erlang:error(badarg)
    end.

% Filter entire family values while retaining the original domain entries.
family_specification(Fun, {'Set', Data, {Domain, [Type]}}) ->
    Selected = select_family(Data, Fun, Type, external(Fun)),
    sofs:from_external(Selected, [{Domain, [Type]}]);
family_specification(_, {'Set', _, '_'} = Family) ->
    Family;
family_specification(_, {'Set', _, _}) ->
    erlang:error(badarg).

% Each family predicate receives the value set, never the domain/value pair.
select_family([], _, _, _) ->
    [];
select_family([{Key, Values} | Rest], Fun, Type, Mode) ->
    Keep =
        case Mode of
            no -> predicate(Fun, clause_sofs:element(Values, Type));
            {yes, External} -> predicate(External, Values)
        end,
    case Keep of
        true -> [{Key, Values} | select_family(Rest, Fun, Type, Mode)];
        false -> select_family(Rest, Fun, Type, Mode)
    end.

% Family projection accepts wrapped functions and retains their result's complete type.
family_projection(_, {'Set', [], {_, [_]}}) ->
    sofs:empty_set();
family_projection(Fun, {'Set', Data, {_, [Type]}} = Family) ->
    case external(Fun) of
        no ->
            {Pairs, NewType} = project_family(Data, Fun, Type, '_'),
            clause_sofs:finish(family_projection, Family, Pairs, NewType);
        _ ->
            erlang:error(badarg)
    end;
family_projection(_, {'Set', _, '_'} = Family) ->
    Family;
family_projection(_, {'Set', _, _}) ->
    erlang:error(badarg).

% Evaluate one function per family value, refining a common output type in domain order.
project_family([], _, _, NewType) ->
    {[], NewType};
project_family([{Key, Values} | Rest], Fun, Type, Previous) ->
    Result = Fun(clause_sofs:element(Values, Type)),
    {External, NewType} = clause_sofs:accept(Result, Previous),
    {Tail, FinalType} = project_family(Rest, Fun, Type, NewType),
    {[{Key, External} | Tail], FinalType}.

% The digraph conversions wait for module digraph (plan 21 step LIB12).
family_to_digraph(_) -> erlang:error({not_implemented, family_to_digraph, 1}).
family_to_digraph(_, _) -> erlang:error({not_implemented, family_to_digraph, 2}).
digraph_to_family(_) -> erlang:error({not_implemented, digraph_to_family, 1}).
digraph_to_family(_, _) -> erlang:error({not_implemented, digraph_to_family, 2}).
